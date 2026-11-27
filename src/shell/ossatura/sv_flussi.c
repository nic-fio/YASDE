/* Servizi veri: flussi video e input (4.2, 4.5, 4.6, 4.10).
 *
 * Un flusso (SvFlusso) tiene una sessione video sul telefono (PhVideoFlusso)
 * legata alla sessione di collegamento in cui e' nata. Il thread di lettura
 * della superficie chiama sv_flusso_leggi (ricezione, decodifica,
 * esportazione); apertura, pausa, ridimensionamento e chiusura girano in
 * ordine su p_flussi; l'input su p_input. Alla caduta il flusso diventa PERSO
 * e al ritorno si riapre da solo (macchina ph_fl_passo, logica.c).
 *
 * Regole per i PhVideoFlusso (componente.h): chi li chiama tiene un "uso"
 * del flusso (sf->uso) e della sessione (sv_sessione_usa); chi li sostituisce o
 * li chiude toglie prima il puntatore e aspetta uso == 0, cosi' nessuna
 * chiamata parte dopo ph_video_chiudi e nessuna lettura gira mentre il
 * flusso si chiude. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sv_interno.h"

#if PH_OSSATURA_SERVIZI_VERI

#define VIDEO_BIT_AL_SECONDO 8000000u /* specifica 2.2: 8 Mbit/s, 60 fps */
#define VIDEO_FPS 60
#define LETTURA_PASSO_MS 100          /* una lettura blocca al massimo cosi' */
#define CODA_INPUT_MAX 512

struct SvFlusso {
    atomic_int rif;           /* 1 del proprietario (si lascia in chiudi_lavoro) + 1 per ogni lavoro in coda */
    Sv *sv;
    GMutex m;
    GCond cv;
    PhSrvFlusso *f;           /* NULL dopo flusso_chiudi: nessun avviso piu' */
    char pacchetto[PH_CFG_LUN_APP];
    int densita;
    uint8_t pagina_apertura;  /* sf->m: pagina della PRIMA apertura (0 = nessuna); azzerata appena
                                 la sessione e' nata, cosi' una riapertura dopo una caduta e' normale */
    int w, h;                 /* misura dichiarata (0 per lo specchio prima di aprire) */
    PhFlStato stato;
    bool gia_aperto;
    bool pausa;               /* voluta dalla shell: si riapplica a ogni riapertura */
    PhVideoFlusso *vf;
    uint64_t sess_id;         /* sessione di collegamento di vf */
    uint32_t id_video;
    int uso;                  /* thread dentro una chiamata ph_video_* su vf */
    atomic_bool interrotto, memoria, chiuso;
    /* Solo il thread di lettura: */
    PhDecodifica *dec;
    bool dec_software;
    bool nuova_decodifica;    /* sf->m: dopo una riapertura il decodificatore riparte */
    bool ripiego_detto;
    bool dec_nato_hw;         /* thread di lettura: il decodificatore attuale e' nato con una via hardware */
    PhVia via_nota;           /* thread di lettura: l'ultima via di decodifica scritta nel registro (AUTOMATICA = nessuna) */
    bool dec_fallita_detta;   /* la riga di registro e' gia' uscita (una per cambio di stato) */
    unsigned scartati;        /* fotogrammi scartati dall'ultima riga di registro */
    /* L'ultima CONFIGURAZIONE (SPS/PPS) ricevuta: si ridanno a ogni
     * decodificatore appena creato (con un codificatore che non antepone le
     * intestazioni ai chiave il decodificatore nuovo resterebbe senza). */
    uint8_t *cfg;
    size_t cfg_n;
    uint64_t cfg_tempo;
    gint64 ultima_chiave_us;  /* ultimo fotogramma chiave chiesto dopo un errore */
    /* Ridimensionamento chiesto in pausa: si applica alla ripresa voluta
     * dalla shell (sf->m). 0 = nessuno. */
    int ridim_w, ridim_h, ridim_d;
    /* Pizzico aperto (sf->m): lo stato lo decide il thread GTK (le fasi che la
     * shell manda); serve a chiudere il gesto (ANNULLA) se il flusso entra in
     * pausa, cambia misura o si chiude, cosi' nessun dito resta giu'. */
    bool pizzico_aperto;
    /* Gesti aperti secondo cio' che il TELEFONO HA DAVVERO RICEVUTO (aggiornati in
     * input_lavoro dopo l'invio riuscito, sf->m): alla chiusura del flusso quelli
     * ancora aperti si annullano (sullo specchio nessun dito resta giu'), anche se
     * un SU accodato e' andato perso; e non parte un ANNULLA per un GIU mai partito. */
    bool tocco_inviato, pizzico_inviato;
    int tc_w, tc_h, tc_x, tc_y;      /* ultimo tocco inviato */
    int pzi_w, pzi_h, pzi_x, pzi_y;  /* ultimo passo di pizzico inviato */
    int pz_w, pz_h, pz_x, pz_y;
    /* Barriera tra input e pausa/ridimensionamento (sf->m): input messi in coda
     * e input gia' inviati al telefono. Gli input non hanno risposta e sono
     * veloci; pausa e ridimensionamento (richieste lente) stanno su p_flussi e
     * aspettano, con scadenza, che gli input accodati PRIMA siano partiti. */
    uint64_t in_accodati, in_inviati;
    /* Screenshot in attesa del prossimo fotogramma decodificato (sf->m). */
    GQueue scatti;
    atomic_int scatti_n;
    /* Registrazione MP4 in corso (sf->m). Il file e' aperto da un lavoro, i
     * pacchetti li aggiunge il thread di lettura, la chiude un lavoro. */
    PhRegistrazione *reg;
    char reg_percorso[PH_CFG_LUN_PERCORSO];
    atomic_bool reg_errore;   /* e' gia' in coda la chiusura per errore */
    /* Il thread di lettura e' dentro sv_flusso_leggi (sf->m): chi libera la
     * registrazione aspetta che ne esca. */
    bool lettore_dentro;
    unsigned lettore_giri;
};

typedef struct {
    PhSrvLavoro *l;
    unsigned dove;
    char *cartella, *app;
    gint64 scade_us;
} Scatto;

static void flusso_libera(SvFlusso *sf);
static void pizzico_chiudi(Sv *sv, SvFlusso *sf);
static void pizzico_chiudi_diretto(Sv *sv, SvFlusso *sf);
static void tocco_chiudi_diretto(Sv *sv, SvFlusso *sf);
static void applica_ridim(SvFlusso *sf, const SvSessione *s, PhVideoFlusso *vf, int w, int h, int d);

/* Ogni lavoro messo in coda per un flusso ne prende un riferimento e lo lascia
 * alla fine: libera chi lascia per ultimo (un lavoro in coda dopo la chiusura
 * non trova un flusso gia' liberato). */
static void sf_prendi(SvFlusso *sf)
{
    atomic_fetch_add(&sf->rif, 1);
}

static void sf_lascia(SvFlusso *sf)
{
    if (atomic_fetch_sub(&sf->rif, 1) == 1)
        flusso_libera(sf);
}

/* ---- Macchina e avvisi (sf->m preso) -------------------------------------------- */
static PhFlPasso passo(SvFlusso *sf, PhFlEvento ev)
{
    PhFlPasso p = ph_fl_passo(sf->stato, ev, sf->gia_aperto);
    sf->stato = p.stato;
    if (p.ha_avviso) {
        PhSrvFlussoDati d = {.evento = p.avviso, .larghezza = sf->w, .altezza = sf->h};
        if (p.avviso == PH_SRV_FL_ERRORE)
            d.frase = ph_t("Il video di questa app si e' interrotto");
        if (sf->f != NULL)
            ph_servizi_flusso_avviso(sf->f, &d);
        if (p.avviso == PH_SRV_FL_APERTO || p.avviso == PH_SRV_FL_RIPRESO)
            sf->gia_aperto = true;
    }
    return p;
}

static void avviso_semplice(SvFlusso *sf, PhSrvFlussoEvento ev, int w, int h, bool protetto)
{
    if (sf->f == NULL)
        return;
    PhSrvFlussoDati d = {.evento = ev, .larghezza = w, .altezza = h, .protetto = protetto};
    ph_servizi_flusso_avviso(sf->f, &d);
}

/* ---- Uso del flusso video (sf->m NON preso) ------------------------------------------- */
/* Torna vf con uso preso, se la sessione s e' quella in cui e' nato. */
static PhVideoFlusso *vf_prendi(SvFlusso *sf, const SvSessione *s)
{
    PhVideoFlusso *vf = NULL;
    g_mutex_lock(&sf->m);
    if (sf->vf != NULL && !atomic_load(&sf->chiuso) && s != NULL && sf->sess_id == s->id) {
        vf = sf->vf;
        sf->uso++;
    }
    g_mutex_unlock(&sf->m);
    return vf;
}

static void vf_lascia(SvFlusso *sf)
{
    g_mutex_lock(&sf->m);
    sf->uso--;
    g_cond_broadcast(&sf->cv);
    g_mutex_unlock(&sf->m);
}


/* ---- Registrazione (10) e screenshot (9): parte comune ---------------------------------------- */
static void emetti_reg(SvFlusso *sf, PhSrvRegEvento ev, const char *percorso, const char *frase)
{
    g_mutex_lock(&sf->m);
    if (sf->f != NULL) {
        PhSrvRegDati d = {.evento = ev, .percorso = percorso, .frase = frase};
        ph_servizi_flusso_registrazione(sf->f, &d);
    }
    g_mutex_unlock(&sf->m);
}

/* Stacca la registrazione, la chiude e avvisa (frase NULL = arresto voluto).
 * Su un lavoro, mai sul thread GTK. ph_registrazione_finisci aspetta una
 * aggiungi in corso; poi si aspetta che il thread di lettura esca dalla
 * chiamata in cui poteva avere il puntatore, e solo allora si libera. */
static void reg_stacca_e_chiudi(SvFlusso *sf, const char *frase, bool avvisa)
{
    char perc[PH_CFG_LUN_PERCORSO];
    g_mutex_lock(&sf->m);
    PhRegistrazione *r = sf->reg;
    sf->reg = NULL;
    g_strlcpy(perc, sf->reg_percorso, sizeof perc);
    g_mutex_unlock(&sf->m);
    if (r == NULL)
        return;
    PhRegistrazioneStato st;
    ph_registrazione_stato(r, &st);
    PhErrore e = {{0}};
    int rc = ph_registrazione_finisci(r, &e);
    g_mutex_lock(&sf->m);
    unsigned g0 = sf->lettore_giri;
    while (sf->lettore_dentro && sf->lettore_giri == g0)
        g_cond_wait(&sf->cv, &sf->m);
    g_mutex_unlock(&sf->m);
    ph_registrazione_libera(r);
    if (avvisa) {
        const char *fr = frase != NULL ? frase : (rc == 0 ? NULL : ph_t("La registrazione e' incompleta"));
        emetti_reg(sf, PH_SRV_REG_FINITA, st.iniziata ? perc : NULL, fr);
    }
}

typedef struct {
    SvFlusso *sf;
    const char *frase; /* letterale tradotto: vale per tutto il programma */
} RegStop;

static void reg_stop_lavoro(Sv *sv, void *dati)
{
    (void)sv;
    RegStop *j = dati;
    SvFlusso *sf = j->sf;
    reg_stacca_e_chiudi(sf, j->frase, !atomic_load(&sf->chiuso));
    atomic_store(&sf->reg_errore, false);
    g_free(j);
    sf_lascia(sf);
}

static void reg_chiedi_stop(SvFlusso *sf, const char *frase)
{
    if (atomic_exchange(&sf->reg_errore, true))
        return; /* gia' in coda */
    RegStop *j = g_new0(RegStop, 1);
    j->sf = sf;
    j->frase = frase;
    sf_prendi(sf);
    sv_lavoro(sf->sv, sf->sv->p_flussi, reg_stop_lavoro, j);
}

static void scatto_libera(Scatto *c)
{
    free(c->cartella);
    free(c->app);
    g_free(c);
}

/* Chiude gli screenshot in coda: tutti (frame != NULL: serviti), oppure solo
 * gli scaduti / tutti con errore. */
static void scatti_servi(SvFlusso *sf, struct AVFrame *av, bool tutti_errore)
{
    GQueue pronti = G_QUEUE_INIT;
    gint64 ora = g_get_monotonic_time();
    g_mutex_lock(&sf->m);
    GList *p = sf->scatti.head;
    while (p != NULL) {
        GList *succ = p->next;
        Scatto *c = p->data;
        if (av != NULL || tutti_errore || c->scade_us <= ora) {
            g_queue_unlink(&sf->scatti, p);
            g_queue_push_tail_link(&pronti, p);
            atomic_fetch_sub(&sf->scatti_n, 1);
        }
        p = succ;
    }
    g_mutex_unlock(&sf->m);
    for (GList *q = pronti.head; q != NULL; q = q->next) {
        Scatto *c = q->data;
        char percorso[PH_CFG_LUN_PERCORSO] = "";
        GBytes *png = NULL;
        PhErrore e = {{0}};
        bool ok = av != NULL;
        const char *frase = ok ? NULL : ph_t("Non c'e' ancora un'immagine da salvare");
        if (ok && (c->dove & PH_SRV_FOTO_FILE) &&
            ph_cattura_screenshot(av, c->cartella, c->app, percorso, sizeof percorso, &e) != 0) {
            ok = false;
            frase = ph_t("Non riesco a salvare lo screenshot");
        }
        if (ok && (c->dove & PH_SRV_FOTO_APPUNTI)) {
            uint8_t *d = NULL;
            size_t n = 0;
            if (ph_cattura_png_memoria(av, &d, &n, &e) == 0) {
                png = g_bytes_new(d, n);
                free(d);
            } else {
                ok = false;
                frase = ph_t("Non riesco a salvare lo screenshot");
            }
        }
        ph_servizi_lavoro_screenshot(c->l, ok ? PH_SRV_ESITO_OK : PH_SRV_ESITO_ERRORE, frase,
                                     ok && percorso[0] != '\0' ? percorso : NULL, ok ? png : NULL);
        if (png != NULL)
            g_bytes_unref(png);
        scatto_libera(c);
    }
    g_list_free(pronti.head);
}

/* ---- Apertura e riapertura -------------------------------------------------------------- */
static void apri_corpo(Sv *sv, SvFlusso *sf)
{
    for (int tentativo = 0; tentativo < 3; tentativo++) {
        if (atomic_load(&sf->chiuso) || atomic_load(&sv->chiusura))
            return;
        SvSessione *s = sv_sessione_usa(sv);
        if (s == NULL) {
            /* Senza sessione: PERSO e si aspetta. Ma se nel frattempo ne e'
             * nata una il gestore potrebbe aver gia' guardato questo flusso
             * (era APRENDO): lo si ricontrolla con il lucchetto preso. */
            bool riprova = false;
            g_mutex_lock(&sf->m);
            passo(sf, PH_FL_EV_APERTO_SENZA_SESSIONE);
            if (sv_sessione_id(sv) != 0)
                riprova = passo(sf, PH_FL_EV_SESSIONE_NUOVA).riapri;
            g_mutex_unlock(&sf->m);
            if (!riprova)
                return;
            continue;
        }
        g_mutex_lock(&sf->m);
        int w = sf->w, h = sf->h, dens = sf->densita;
        int usw = 0, ush = 0, usd = 0; /* il ridimensionamento in attesa usato per aprire */
        bool specchio = sf->pacchetto[0] == '\0';
        uint8_t pag = specchio ? 0 : sf->pagina_apertura;
        if (!specchio && sf->ridim_w > 0) { /* misura chiesta in pausa o a flusso assente: si apre gia' cosi' */
            w = usw = sf->ridim_w;
            h = ush = sf->ridim_h;
            if (sf->ridim_d > 0)
                dens = usd = sf->ridim_d;
        }
        g_mutex_unlock(&sf->m);
        if (specchio)
            ph_ossatura_misura_specchio(s->schermo_w, s->schermo_h, &w, &h);
        if (dens <= 0)
            dens = s->densita > 0 ? s->densita : 420;
        PhVideoRichiesta rq = {.larghezza = (uint16_t)w, .altezza = (uint16_t)h, .densita = (uint16_t)dens,
                               .bit_al_secondo = VIDEO_BIT_AL_SECONDO, .fps = VIDEO_FPS,
                               .pacchetto = specchio ? NULL : sf->pacchetto, .pagina = pag};
        PhErrore e = {{0}};
        PhVideoFlusso *vf = ph_video_apri(s->c, &rq, &e);
        bool viva;
        g_mutex_lock(&s->m);
        viva = s->attiva;
        g_mutex_unlock(&s->m);
        if (vf == NULL) {
            bool riprova = false;
            g_mutex_lock(&sf->m);
            if (viva) {
                passo(sf, PH_FL_EV_APERTO_FALLITO); /* il telefono ha rifiutato: ERRORE */
            } else {
                passo(sf, PH_FL_EV_APERTO_SENZA_SESSIONE);
                if (sv_sessione_id(sv) != 0)
                    riprova = passo(sf, PH_FL_EV_SESSIONE_NUOVA).riapri;
            }
            g_mutex_unlock(&sf->m);
            sv_sessione_lascia(s);
            if (!riprova)
                return;
            continue;
        }

        /* Installa il flusso nuovo al posto del vecchio (quello della sessione
         * caduta, se c'e'): prima lo si toglie, poi si aspetta chi lo usa. */
        PhVideoFlusso *vecchio = NULL;
        bool scarta = false, pausa = false;
        int pw = 0, pa = 0, pd = 0; /* ridimensionamento arrivato MENTRE si apriva: va applicato dopo */
        g_mutex_lock(&sf->m);
        vecchio = sf->vf;
        sf->vf = NULL;
        while (sf->uso > 0)
            g_cond_wait(&sf->cv, &sf->m);
        g_mutex_lock(&s->m);
        viva = s->attiva;
        g_mutex_unlock(&s->m);
        if (atomic_load(&sf->chiuso) || !viva) {
            scarta = true;
            if (!atomic_load(&sf->chiuso))
                passo(sf, PH_FL_EV_APERTO_SENZA_SESSIONE);
        } else {
            sf->vf = vf;
            sf->sess_id = s->id;
            sf->id_video = ph_video_id(vf);
            sf->w = w;
            sf->h = h;
            sf->nuova_decodifica = true;
            sf->pagina_apertura = 0; /* usata: mai alla riapertura */
            if (usd > 0) /* la densita' usata per aprire e' ora quella del flusso */
                sf->densita = usd;
            if (sf->ridim_w > 0 && (sf->ridim_w != usw || sf->ridim_h != ush || sf->ridim_d != usd)) {
                pw = sf->ridim_w;
                pa = sf->ridim_h;
                pd = sf->ridim_d;
            }
            sf->ridim_w = sf->ridim_h = sf->ridim_d = 0;
            pausa = sf->pausa;
            passo(sf, PH_FL_EV_APERTO); /* APERTO la prima volta, RIPRESO dopo */
        }
        g_mutex_unlock(&sf->m);
        if (vecchio != NULL)
            ph_video_chiudi(vecchio, false); /* nessuno lo usa piu' */
        if (scarta)
            ph_video_chiudi(vf, false);
        else {
            if (pausa) { /* la shell lo voleva in pausa (host minimizzato, ...) */
                PhVideoFlusso *u = vf_prendi(sf, s);
                if (u != NULL) {
                    (void)ph_video_pausa(u, &e);
                    vf_lascia(sf);
                }
            }
            if (pw > 0) {
                if (pausa) { /* in pausa si ricorda: la ripresa lo applica */
                    g_mutex_lock(&sf->m);
                    sf->ridim_w = pw;
                    sf->ridim_h = pa;
                    sf->ridim_d = pd;
                    g_mutex_unlock(&sf->m);
                } else {
                    PhVideoFlusso *u = vf_prendi(sf, s);
                    if (u != NULL) {
                        applica_ridim(sf, s, u, pw, pa, pd);
                        vf_lascia(sf);
                    }
                }
            }
            sv_audio_programma(sv); /* il primo flusso della sessione fa partire il conto */
        }
        sv_sessione_lascia(s);
        return;
    }
}

static void apri_lavoro(Sv *sv, void *dati)
{
    SvFlusso *sf = dati;
    apri_corpo(sv, sf);
    sf_lascia(sf);
}

void *sv_flusso_apri(void *b, PhSrvFlusso *f, const PhSrvFlussoRichiesta *r, PhErrore *e)
{
    Sv *sv = b;
    if (atomic_load(&sv->chiusura)) {
        ph_errore(e, "flusso: i servizi si stanno chiudendo");
        return NULL;
    }
    const char *pk = r->pacchetto != NULL ? r->pacchetto : "";
    if (strlen(pk) >= PH_CFG_LUN_APP) {
        ph_errore(e, "flusso: nome del pacchetto troppo lungo");
        return NULL;
    }
    SvFlusso *sf = g_new0(SvFlusso, 1);
    atomic_init(&sf->rif, 1);
    sf->sv = sv;
    g_mutex_init(&sf->m);
    g_cond_init(&sf->cv);
    sf->f = f;
    g_strlcpy(sf->pacchetto, pk, sizeof sf->pacchetto);
    sf->densita = r->densita;
    sf->pagina_apertura = r->pagina;
    sf->w = r->larghezza;
    sf->h = r->altezza;
    sf->stato = PH_FL_APRENDO;
    atomic_init(&sf->interrotto, false);
    atomic_init(&sf->memoria, false);
    atomic_init(&sf->chiuso, false);
    atomic_init(&sf->scatti_n, 0);
    atomic_init(&sf->reg_errore, false);
    g_queue_init(&sf->scatti);

    g_mutex_lock(&sv->m);
    if (sv->flussi->len >= SV_FLUSSI_MAX) {
        g_mutex_unlock(&sv->m);
        g_mutex_clear(&sf->m);
        g_cond_clear(&sf->cv);
        g_free(sf);
        ph_errore(e, "flusso: troppi flussi aperti");
        return NULL;
    }
    g_ptr_array_add(sv->flussi, sf);
    /* Messo in coda dentro il lucchetto: ricerche e riaperture non lo
     * dimenticano. */
    sf_prendi(sf);
    sv_lavoro(sv, sv->p_flussi, apri_lavoro, sf);
    g_mutex_unlock(&sv->m);
    return sf;
}

/* ---- Lettura (thread della superficie) ------------------------------------------------------ */
/* Il punto d'aggancio dell'esportazione: AVFrame -> PhFotogramma (src/video/esporta.h). */
static int esporta(SvFlusso *sf, struct AVFrame **av, PhFotogramma *out)
{
    PhErrore e = {{0}};
    int via = ph_esporta(av, out, &e);
    if (via < 0) {
        /* Una riga alla prima volta; poi solo il conteggio, quando riparte. */
        if (sf->scartati++ == 0)
            ph_shell_decisione("fotogramma scartato", "esportazione non riuscita");
        return 0;
    }
    if (sf->scartati > 0) {
        ph_shell_decisione("fotogrammi scartati", "%u in tutto prima di questo", sf->scartati);
        sf->scartati = 0;
    }
    if (via == PH_ESPORTA_RIPIEGO && !sf->ripiego_detto) {
        sf->ripiego_detto = true;
        ph_shell_decisione("flusso in memoria invece che DMA-BUF", "fotogramma della scheda grafica non mappabile, copiato in memoria");
    }
    return 1;
}

/* Dopo un errore di decodifica l'immagine resta rovinata fino al prossimo
 * chiave (fino a 10 s): se ne chiede uno, al massimo ogni 500 ms. vf e' in
 * uso del thread di lettura. */
static void chiedi_chiave_dopo_errore(SvFlusso *sf, PhVideoFlusso *vf)
{
    gint64 ora = g_get_monotonic_time();
    if (ora - sf->ultima_chiave_us < 500 * 1000)
        return;
    sf->ultima_chiave_us = ora;
    PhErrore e = {{0}};
    (void)ph_video_chiave(vf, &e);
}

/* Un fotogramma pronto, se il decodificatore ne ha uno. 1 = *out riempito. */
static int fotogramma_pronto(SvFlusso *sf, PhVideoFlusso *vf, PhFotogramma *out)
{
    struct AVFrame *av = NULL;
    PhErrore e = {{0}};
    int r = ph_decodifica_prendi(sf->dec, &av, &e);
    if (r < 0)
        chiedi_chiave_dopo_errore(sf, vf);
    if (r != 1)
        return 0;
    /* Gli screenshot si prendono qui, PRIMA di ph_esporta che consuma il
     * fotogramma: lo stesso thread del decodificatore (cattura.h). */
    if (atomic_load(&sf->scatti_n) > 0)
        scatti_servi(sf, av, false);
    return esporta(sf, &av, out);
}

/* Quale via sta usando DAVVERO il decodificatore (ph_decodifica_via tiene conto anche del
 * ripiego a sessione avviata, che si scopre al primo pacchetto: per questo si richiama dopo
 * ogni pacchetto dato). Una riga di registro per flusso e per cambio di via. Se e' il
 * processore parte l'avviso a schermo, UNO per sessione di collegamento (sv->decod_sw_sess):
 * non per fotogramma, non per finestra, non a ogni ricreazione del decodificatore, e se si
 * torna all'hardware e si ricade nel processore nella stessa sessione non si ripete.
 * L'avviso passa dai servizi (consegna sul thread GTK): da qui non si tocca GTK. Solo il
 * thread di lettura, nessun lock preso. */
static bool decodificatore_nuovo(Sv *sv, SvFlusso *sf, PhVideoFlusso *vf, bool software);

/* true se ha ricreato il decodificatore (ripiego a sessione avviata): chi chiama lascia
 * perdere il pacchetto in corso, il decodificatore nuovo riparte dal chiave chiesto qui. */
static bool via_verifica(Sv *sv, SvFlusso *sf, PhVideoFlusso *vf)
{
    if (sf->dec == NULL)
        return false;
    PhVia via = ph_decodifica_via(sf->dec);
    if (via == sf->via_nota)
        return false;
    if (via != PH_VIA_SOFTWARE) {
        ph_shell_decisione("decodifica video", "via %s (scheda grafica)", ph_via_nome(via));
        sf->via_nota = via;
        return false;
    }
    uint64_t sess = atomic_load(&sv->sess_id);
    if (sess == 0)
        return false; /* nessuna sessione: si riprova al prossimo pacchetto */
    /* La causa: GTK, oppure un decodificatore nato hardware e poi passato al software da solo
     * (ripiego), oppure nessuna via hardware gia' alla creazione. */
    const char *causa = atomic_load(&sf->memoria) && sf->dec_software ? "GTK ha rifiutato il DMA-BUF"
                        : sf->dec_nato_hw                                ? "la via hardware non regge il flusso"
                                                                         : "nessuna via hardware disponibile";
    ph_shell_decisione("decodifica col processore", "%s: prestazioni ridotte", causa);
    if (atomic_exchange(&sv->decod_sw_sess, sess) != sess)
        ph_servizi_pubblica_decodifica_software(sv->serv);
    sf->via_nota = via;
    if (!sf->dec_nato_hw)
        return false;
    /* Ripiego interno: ph_decodifica_dai tornerebbe errore a OGNI pacchetto e si chiederebbero
     * chiavi di continuo. Si passa a un decodificatore software vero (una volta), con la
     * configurazione e UN fotogramma chiave. */
    (void)decodificatore_nuovo(sv, sf, vf, true);
    return true;
}

/* Crea (o ricrea) il decodificatore e gli da' l'ultima CONFIGURAZIONE. false
 * se non si crea. */
static bool decodificatore_nuovo(Sv *sv, SvFlusso *sf, PhVideoFlusso *vf, bool software)
{
    PhErrore e = {{0}};
    bool cambio_via = sf->dec != NULL && software && !sf->dec_software;
    if (sf->dec != NULL)
        ph_decodifica_libera(sf->dec);
    sf->dec = ph_decodifica_crea(software ? PH_VIA_SOFTWARE : PH_VIA_AUTOMATICA, &e);
    sf->dec_software = software;
    sf->dec_nato_hw = sf->dec != NULL && ph_decodifica_via(sf->dec) != PH_VIA_SOFTWARE;
    if (sf->dec == NULL) {
        if (!sf->dec_fallita_detta) { /* una riga per cambio di stato, non venti al secondo */
            sf->dec_fallita_detta = true;
            ph_shell_decisione("decodifica non disponibile", "il decodificatore non si e' creato");
        }
        return false;
    }
    if (sf->dec_fallita_detta) {
        sf->dec_fallita_detta = false;
        ph_shell_decisione("decodifica di nuovo disponibile", "il decodificatore si e' creato");
    }
    if (sf->cfg != NULL)
        (void)ph_decodifica_dai(sf->dec, true, sf->cfg, sf->cfg_n, sf->cfg_tempo, &e);
    if (cambio_via) {
        if (atomic_load(&sf->memoria))
            ph_shell_decisione("flusso in memoria invece che DMA-BUF", "GTK ha rifiutato il DMA-BUF: decodifica software");
        (void)ph_video_chiave(vf, &e);
    }
    (void)via_verifica(sv, sf, vf);
    return true;
}

/* Legge dal telefono, con vf e la sessione gia' in uso. */
static int leggi_uno(Sv *sv, SvFlusso *sf, PhVideoFlusso *vf, int resto_ms, PhFotogramma *out)
{
    PhErrore e = {{0}};
    bool nuova;
    g_mutex_lock(&sf->m);
    nuova = sf->nuova_decodifica;
    sf->nuova_decodifica = false;
    g_mutex_unlock(&sf->m);
    bool voglio_sw = atomic_load(&sf->memoria);
    if (sf->dec == NULL || nuova || (voglio_sw && !sf->dec_software)) {
        if (!decodificatore_nuovo(sv, sf, vf, voglio_sw)) {
            g_usleep(50 * 1000);
            return 0;
        }
    }
    if (fotogramma_pronto(sf, vf, out))
        return 1;

    PhVideoPacchetto p;
    int attesa = resto_ms < LETTURA_PASSO_MS ? resto_ms : LETTURA_PASSO_MS;
    int rc = ph_video_leggi(vf, &p, attesa, &e);
    if (rc == PH_CANALE_TEMPO)
        return 0;
    if (rc != 1) { /* finito o in errore: eventi e riapertura se ne occupano; non si gira a vuoto */
        g_usleep(50 * 1000);
        return 0;
    }
    if (p.configurazione) {
        /* L'ultima CONFIGURAZIONE si conserva (al massimo 64 KiB). */
        if (p.dati != NULL && p.lunghezza > 0 && p.lunghezza <= 65536) {
            uint8_t *c = malloc(p.lunghezza);
            if (c != NULL) {
                memcpy(c, p.dati, p.lunghezza);
                free(sf->cfg);
                sf->cfg = c;
                sf->cfg_n = p.lunghezza;
                sf->cfg_tempo = p.tempo_us;
            }
        }
        if (p.larghezza > 0 && p.altezza > 0) {
            g_mutex_lock(&sf->m);
            if (p.larghezza != sf->w || p.altezza != sf->h) {
                sf->w = p.larghezza;
                sf->h = p.altezza;
                avviso_semplice(sf, PH_SRV_FL_MISURA, sf->w, sf->h, false);
            }
            g_mutex_unlock(&sf->m);
        }
    }
    /* OGNI pacchetto (configurazioni comprese) va alla registrazione prima di
     * decodificare: H.264 cosi' come arriva, nessuna ricodifica. */
    g_mutex_lock(&sf->m);
    PhRegistrazione *rg = sf->reg;
    g_mutex_unlock(&sf->m);
    if (rg != NULL &&
        ph_registrazione_aggiungi_video(rg, p.configurazione, p.chiave, p.larghezza, p.altezza, p.tempo_us, p.dati,
                                        p.lunghezza, &e) != 0)
        reg_chiedi_stop(sf, ph_t("La registrazione si e' fermata prima della fine"));
    /* Un pacchetto rovinato non e' fatale (decodifica.h): si chiede un chiave e
     * si va avanti. */
    int dai = ph_decodifica_dai(sf->dec, p.configurazione, p.dati, p.lunghezza, p.tempo_us, &e);
    /* Il ripiego al processore si scopre qui, anche se dai torna -1. */
    if (via_verifica(sv, sf, vf))
        return 0;
    if (dai != 0) {
        chiedi_chiave_dopo_errore(sf, vf);
        return 0;
    }
    return fotogramma_pronto(sf, vf, out);
}

int sv_flusso_leggi(void *b, void *fl, PhFotogramma *out, int timeout_ms)
{
    /* Dopo ph_servizi_libera il pezzo comune passa b = NULL: un widget che
     * sopravvive alla chiusura (qualcuno tiene un riferimento) non deve arrivare
     * qui a toccare un backend gia' liberato. I flussi rimasti li ha liberati
     * sv_flussi_chiudi_finale. */
    if (b == NULL)
        return -1;
    Sv *sv = b;
    SvFlusso *sf = fl;
    if (atomic_load(&sf->interrotto) || atomic_load(&sf->chiuso))
        return -1;
    if (timeout_ms < 1)
        timeout_ms = 1;
    /* Nessun fotogramma entro 2 s (schermo fermo, pausa, connessione persa). */
    if (atomic_load(&sf->scatti_n) > 0)
        scatti_servi(sf, NULL, false);
    /* La sessione resta in uso per tutta la lettura: il componente non puo'
     * sparire sotto ph_video_leggi. */
    SvSessione *s = sv_sessione_usa(sv);
    PhVideoFlusso *vf = s != NULL ? vf_prendi(sf, s) : NULL;
    if (vf == NULL) {
        /* PERSO o in apertura: niente da leggere, si aspetta un po'. */
        if (s != NULL)
            sv_sessione_lascia(s);
        g_usleep((timeout_ms < 20 ? timeout_ms : 20) * 1000);
        return atomic_load(&sf->interrotto) ? -1 : 0;
    }
    g_mutex_lock(&sf->m);
    sf->lettore_dentro = true;
    g_mutex_unlock(&sf->m);
    int r = leggi_uno(sv, sf, vf, timeout_ms, out);
    g_mutex_lock(&sf->m);
    sf->lettore_dentro = false;
    sf->lettore_giri++;
    g_cond_broadcast(&sf->cv);
    g_mutex_unlock(&sf->m);
    vf_lascia(sf);
    sv_sessione_lascia(s);
    if (r == 1 && atomic_load(&sf->interrotto)) {
        ph_fotogramma_rilascia(out);
        return -1;
    }
    return r;
}

void sv_flusso_interrompi(void *b, void *fl)
{
    /* Dopo ph_servizi_libera il pezzo comune passa b = NULL: un widget che
     * sopravvive alla chiusura (qualcuno tiene un riferimento) non deve arrivare
     * qui a toccare un backend gia' liberato. I flussi rimasti li ha liberati
     * sv_flussi_chiudi_finale. */
    if (b == NULL)
        return;
    (void)b;
    atomic_store(&((SvFlusso *)fl)->interrotto, true);
}

void sv_flusso_memoria(void *b, void *fl)
{
    /* Dopo ph_servizi_libera il pezzo comune passa b = NULL: un widget che
     * sopravvive alla chiusura (qualcuno tiene un riferimento) non deve arrivare
     * qui a toccare un backend gia' liberato. I flussi rimasti li ha liberati
     * sv_flussi_chiudi_finale. */
    if (b == NULL)
        return;
    (void)b;
    atomic_store(&((SvFlusso *)fl)->memoria, true);
}

/* ---- Pausa, ridimensionamento ----------------------------------------------------------------- */
typedef struct {
    SvFlusso *sf;
    bool pausa;
    int w, h, d;       /* ridimensionamento: misura e densita' (0 = invariata) */
    uint64_t barriera; /* input accodati al momento della richiesta */
    uint8_t pagina;    /* pagina (1 o 2) da mandare al telefono */
    bool in_pausa;     /* la pausa voluta al momento della richiesta (ridimensionamento): i lavori
                          vanno in ordine, ma il flag della shell cambia subito */
} OpJob;

/* Aspetta (al massimo 2 s) che gli input accodati prima della richiesta siano
 * partiti verso il telefono: cosi' la pausa non sorpassa l'annullamento di un
 * tocco, ma i tocchi non aspettano mai una risposta del telefono. */
static void barriera_input_c(SvFlusso *sf, uint64_t barriera, bool anche_chiuso)
{
    Sv *sv = sf->sv;
    /* Alla chiusura (anche_chiuso) i flussi si chiudono uno dopo l'altro su un solo
     * thread: se il canale dell'input e' fermo la prima barriera scade (2 s) e le
     * successive non aspettano piu' (altrimenti N x 2 s). Riparte ad aspettare
     * quando una barriera finisce regolarmente. */
    if (anche_chiuso) {
        g_mutex_lock(&sf->m);
        bool pronta = sf->in_inviati >= barriera;
        g_mutex_unlock(&sf->m);
        if (pronta) { /* gli input di questo flusso sono tutti partiti: il canale e' vivo */
            if (barriera > 0)
                atomic_store(&sv->barriera_ferma, false);
            return;
        }
        if (atomic_load(&sv->barriera_ferma))
            return;
    }
    gint64 scade = g_get_monotonic_time() + 2 * G_USEC_PER_SEC;
    bool atteso = false;
    g_mutex_lock(&sf->m);
    while (sf->in_inviati < barriera && (anche_chiuso || !atomic_load(&sf->chiuso))) {
        atteso = true;
        if (!g_cond_wait_until(&sf->cv, &sf->m, scade))
            break;
    }
    bool scaduta = sf->in_inviati < barriera;
    g_mutex_unlock(&sf->m);
    if (anche_chiuso) {
        if (scaduta)
            atomic_store(&sv->barriera_ferma, true);
        else if (atteso)
            atomic_store(&sv->barriera_ferma, false);
    }
}

static void barriera_input(SvFlusso *sf, uint64_t barriera)
{
    barriera_input_c(sf, barriera, false);
}

/* Applica una nuova misura (e densita': 0 = quella ricordata) al telefono, con
 * vf e la sessione in uso. La densita' applicata si ricorda con la misura:
 * serve a riaprire la sessione identica dopo una caduta. */
static void applica_ridim(SvFlusso *sf, const SvSessione *s, PhVideoFlusso *vf, int w, int h, int d)
{
    g_mutex_lock(&sf->m);
    int dens = d > 0 ? d : sf->densita;
    g_mutex_unlock(&sf->m);
    if (dens <= 0)
        dens = s->densita > 0 ? s->densita : 420;
    PhErrore e = {{0}};
    uint16_t wv = 0, hv = 0;
    if (ph_video_ridimensiona(vf, (uint16_t)w, (uint16_t)h, (uint16_t)dens, &wv, &hv, &e) == 0) {
        g_mutex_lock(&sf->m);
        if (d > 0) /* ricordata solo dopo che il telefono l'ha applicata */
            sf->densita = d;
        if (wv != sf->w || hv != sf->h) {
            sf->w = wv;
            sf->h = hv;
            avviso_semplice(sf, PH_SRV_FL_MISURA, wv, hv, false);
        }
        g_mutex_unlock(&sf->m);
    }
}

/* Pausa, ripresa e ridimensionamento stanno su p_flussi (richieste lente al
 * telefono) con una barriera per flusso (barriera_input): la VIDEO_PAUSA non
 * arriva al telefono prima dell'annullamento del tocco che la shell manda
 * subito prima (altrimenti un dito resterebbe "giu'" sullo schermo virtuale),
 * ma l'input di tutte le finestre non aspetta mai una richiesta lenta. */
static void pausa_lavoro(Sv *sv, void *dati)
{
    OpJob *j = dati;
    SvFlusso *sf = j->sf;
    barriera_input(sf, j->barriera);
    SvSessione *s = sv_sessione_usa(sv);
    PhVideoFlusso *vf = (s != NULL && !atomic_load(&sf->chiuso)) ? vf_prendi(sf, s) : NULL;
    if (vf != NULL) {
        PhErrore e = {{0}};
        /* Due chiamate di fila non sono un errore; se la sessione e' caduta la
         * pausa voluta si riapplica alla riapertura. */
        int r = j->pausa ? ph_video_pausa(vf, &e) : ph_video_riprendi(vf, &e);
        if (r == 0 && !j->pausa) {
            /* Alla ripresa voluta dalla shell si applica la misura chiesta in pausa. */
            g_mutex_lock(&sf->m);
            int w = sf->ridim_w, h = sf->ridim_h, d = sf->ridim_d;
            sf->ridim_w = sf->ridim_h = sf->ridim_d = 0;
            g_mutex_unlock(&sf->m);
            if (w > 0) /* misura E densita' chieste in pausa */
                applica_ridim(sf, s, vf, w, h, d);
        }
        vf_lascia(sf);
    }
    if (s != NULL)
        sv_sessione_lascia(s);
    g_free(j);
    sf_lascia(sf);
}

void sv_flusso_pausa(void *b, void *fl, bool pausa)
{
    /* Dopo ph_servizi_libera il pezzo comune passa b = NULL: un widget che
     * sopravvive alla chiusura (qualcuno tiene un riferimento) non deve arrivare
     * qui a toccare un backend gia' liberato. I flussi rimasti li ha liberati
     * sv_flussi_chiudi_finale. */
    if (b == NULL)
        return;
    Sv *sv = b;
    SvFlusso *sf = fl;
    if (pausa) /* un pizzico aperto si annulla PRIMA della pausa: nessun dito resta giu' */
        pizzico_chiudi(sv, sf);
    g_mutex_lock(&sf->m);
    sf->pausa = pausa;
    uint64_t barriera = sf->in_accodati;
    g_mutex_unlock(&sf->m);
    OpJob *j = g_new0(OpJob, 1);
    j->sf = sf;
    j->pausa = pausa;
    j->barriera = barriera;
    sf_prendi(sf);
    sv_lavoro(sv, sv->p_flussi, pausa_lavoro, j);
}

void (*sv_prova_ridim_fatto)(void) = NULL;

static void ridim_lavoro(Sv *sv, void *dati)
{
    OpJob *j = dati;
    SvFlusso *sf = j->sf;
    barriera_input(sf, j->barriera);
    SvSessione *s = sv_sessione_usa(sv);
    PhVideoFlusso *vf = (s != NULL && !atomic_load(&sf->chiuso)) ? vf_prendi(sf, s) : NULL;
    g_mutex_lock(&sf->m);
    bool specchio = sf->pacchetto[0] == '\0';
    bool in_pausa = j->in_pausa; /* com'era alla richiesta, non adesso */
    /* In pausa (2.2: non si ridimensiona) o con il flusso assente (PERSO, in
     * apertura): la si ricorda, la applica la ripresa o la riapertura. */
    if ((in_pausa || vf == NULL) && !specchio) {
        sf->ridim_w = j->w;
        sf->ridim_h = j->h;
        if (j->d > 0)
            sf->ridim_d = j->d;
    }
    g_mutex_unlock(&sf->m);
    if (vf != NULL && !specchio && !in_pausa)
        applica_ridim(sf, s, vf, j->w, j->h, j->d);
    if (vf != NULL)
        vf_lascia(sf);
    if (s != NULL)
        sv_sessione_lascia(s);
    if (sv_prova_ridim_fatto != NULL)
        sv_prova_ridim_fatto();
    g_free(j);
    sf_lascia(sf);
}

void sv_flusso_ridimensiona_densita(void *b, void *fl, int larghezza, int altezza, int densita)
{
    /* Dopo ph_servizi_libera il pezzo comune passa b = NULL: un widget che
     * sopravvive alla chiusura (qualcuno tiene un riferimento) non deve arrivare
     * qui a toccare un backend gia' liberato. I flussi rimasti li ha liberati
     * sv_flussi_chiudi_finale. */
    if (b == NULL)
        return;
    Sv *sv = b;
    OpJob *j = g_new0(OpJob, 1);
    SvFlusso *sf = fl;
    j->sf = sf;
    j->w = larghezza;
    j->h = altezza;
    j->d = densita;
    pizzico_chiudi(sv, sf); /* un pizzico aperto si annulla prima di cambiare misura */
    g_mutex_lock(&sf->m);
    j->barriera = sf->in_accodati;
    j->in_pausa = sf->pausa;
    g_mutex_unlock(&sf->m);
    sf_prendi(sf);
    sv_lavoro(sv, sv->p_flussi, ridim_lavoro, j);
}

void sv_flusso_ridimensiona(void *b, void *fl, int larghezza, int altezza)
{
    sv_flusso_ridimensiona_densita(b, fl, larghezza, altezza, 0);
}

/* ---- Fotogramma chiave a richiesta (8) --------------------------------------------------------- */
static void chiave_corpo(Sv *sv, SvFlusso *sf)
{
    SvSessione *s = sv_sessione_usa(sv);
    PhVideoFlusso *vf = (s != NULL && !atomic_load(&sf->chiuso)) ? vf_prendi(sf, s) : NULL;
    if (vf != NULL) {
        g_mutex_lock(&sf->m);
        bool in_pausa = sf->pausa; /* in pausa il telefono risponde ERRORE: non si manda */
        g_mutex_unlock(&sf->m);
        PhErrore e = {{0}};
        if (!in_pausa)
            (void)ph_video_chiave(vf, &e);
        vf_lascia(sf);
    }
    if (s != NULL)
        sv_sessione_lascia(s);
}

static void chiave_lavoro(Sv *sv, void *dati)
{
    chiave_corpo(sv, dati);
    sf_lascia(dati);
}

void sv_flusso_chiave(void *b, void *fl)
{
    /* Dopo ph_servizi_libera il pezzo comune passa b = NULL: un widget che
     * sopravvive alla chiusura (qualcuno tiene un riferimento) non deve arrivare
     * qui a toccare un backend gia' liberato. I flussi rimasti li ha liberati
     * sv_flussi_chiudi_finale. */
    if (b == NULL)
        return;
    Sv *sv = b;
    if (!atomic_load(&((SvFlusso *)fl)->chiuso)) {
        sf_prendi(fl);
        sv_lavoro(sv, sv->p_flussi, chiave_lavoro, fl);
    }
}

/* ---- Pagina su un flusso aperto (27 nov) -------------------------------------------------------
 * Come il chiave: richiesta lenta, gira su p_flussi, con vf e la sessione in uso (se il flusso si
 * chiude nel frattempo vf_prendi non lo da' e nulla parte; chi chiude aspetta uso == 0). Nessuna
 * risposta alla shell: un rifiuto del telefono, la pausa o l'assenza di sessione si scrivono su
 * stderr (una frase fissa, nessun dato) e il flusso resta com'era. */
void (*sv_prova_pagina_fatto)(int esito) = NULL; /* solo prove: 0 riuscita, -1 rifiutata, 1 non mandata */

/* Gli stessi numeri nei tre elenchi (servizi, shell, protocollo): un cambio in uno solo manderebbe
 * al telefono una pagina diversa da quella chiesta. */
_Static_assert((int)PH_SRV_PAGINA_NESSUNA == (int)PH_SHELL_PAGINA_NESSUNA &&
                   (int)PH_SRV_PAGINA_NESSUNA == (int)PH_VIDEO_PAGINA_NESSUNA &&
                   (int)PH_SRV_PAGINA_BATTERIA == (int)PH_SHELL_PAGINA_BATTERIA &&
                   (int)PH_SRV_PAGINA_BATTERIA == (int)PH_VIDEO_PAGINA_BATTERIA &&
                   (int)PH_SRV_PAGINA_MEMORIA == (int)PH_SHELL_PAGINA_MEMORIA &&
                   (int)PH_SRV_PAGINA_MEMORIA == (int)PH_VIDEO_PAGINA_MEMORIA,
               "pagine: valori diversi tra PH_SRV_PAGINA_*, PhShellPagina e PhVideoPagina");

static void pagina_lavoro(Sv *sv, void *dati)
{
    OpJob *j = dati;
    SvFlusso *sf = j->sf;
    int esito = 1;
    SvSessione *s = sv_sessione_usa(sv);
    PhVideoFlusso *vf = (s != NULL && !atomic_load(&sf->chiuso)) ? vf_prendi(sf, s) : NULL;
    if (vf != NULL) {
        /* In pausa il telefono risponde ERRORE: non si manda. La pausa e' quella di quando la
         * pagina e' stata chiesta (j->in_pausa), non di quando il lavoro parte: i lavori vanno in
         * ordine, ma il flag della shell cambia subito. */
        if (!j->in_pausa) {
            PhErrore e = {{0}};
            esito = ph_video_pagina(vf, j->pagina, &e) == 0 ? 0 : -1;
        }
        vf_lascia(sf);
    }
    if (s != NULL)
        sv_sessione_lascia(s);
    if (esito != 0 && !atomic_load(&sf->chiuso))
        fprintf(stderr, "ossatura: pagina %u non aperta sul telefono (%s)\n", (unsigned)j->pagina,
                esito < 0 ? "rifiutata" : "flusso non pronto o in pausa");
    if (sv_prova_pagina_fatto != NULL)
        sv_prova_pagina_fatto(esito);
    g_free(j);
    sf_lascia(sf);
}

void sv_flusso_pagina(void *b, void *fl, uint8_t pagina)
{
    /* Dopo ph_servizi_libera il pezzo comune passa b = NULL (vedi sv_flusso_chiave). */
    if (b == NULL)
        return;
    Sv *sv = b;
    SvFlusso *sf = fl;
    if (atomic_load(&sf->chiuso))
        return;
    OpJob *j = g_new0(OpJob, 1);
    j->sf = sf;
    j->pagina = pagina;
    g_mutex_lock(&sf->m);
    j->in_pausa = sf->pausa;
    g_mutex_unlock(&sf->m);
    sf_prendi(sf);
    sv_lavoro(sv, sv->p_flussi, pagina_lavoro, j);
}

/* ---- Screenshot e registrazione (tabella, thread GTK: non bloccano) -------------------------------- */
void sv_flusso_screenshot(void *b, void *fl, PhSrvLavoro *l, unsigned dove, const char *cartella_pc,
                          const char *nome_app)
{
    SvFlusso *sf = fl;
    Scatto *c = g_new0(Scatto, 1);
    c->l = l;
    c->dove = dove;
    c->cartella = strdup(cartella_pc != NULL ? cartella_pc : "");
    c->app = strdup(nome_app != NULL ? nome_app : "");
    /* Nessuna immagine entro 2 s -> ERRORE (la controlla il thread di lettura). */
    c->scade_us = g_get_monotonic_time() + 2 * G_USEC_PER_SEC;
    if (c->cartella == NULL || c->app == NULL || atomic_load(&sf->chiuso)) {
        ph_servizi_lavoro_screenshot(l, PH_SRV_ESITO_ERRORE, ph_t("Non riesco a salvare lo screenshot"), NULL, NULL);
        scatto_libera(c);
        return;
    }
    g_mutex_lock(&sf->m);
    g_queue_push_tail(&sf->scatti, c);
    atomic_fetch_add(&sf->scatti_n, 1);
    g_mutex_unlock(&sf->m);
    /* Con lo schermo fermo il telefono non manda nulla: si chiede un chiave. */
    sv_flusso_chiave(b, fl);
}

typedef struct {
    SvFlusso *sf;
    bool avvia;
    char *cartella, *app;
} RegJob;

static void reg_lavoro(Sv *sv, void *dati)
{
    RegJob *j = dati;
    SvFlusso *sf = j->sf;
    if (!j->avvia) {
        reg_stacca_e_chiudi(sf, NULL, !atomic_load(&sf->chiuso));
    } else {
        g_mutex_lock(&sf->m);
        bool gia = sf->reg != NULL;
        g_mutex_unlock(&sf->m);
        char perc[PH_CFG_LUN_PERCORSO];
        PhErrore e = {{0}};
        /* Senza traccia audio: audio.h non da' i pacchetti Opus (rapporto). */
        PhRegistrazione *r = (gia || atomic_load(&sf->chiuso))
                                 ? NULL
                                 : ph_registrazione_inizia(j->cartella, j->app, false, perc, sizeof perc, &e);
        if (r == NULL) {
            emetti_reg(sf, PH_SRV_REG_ERRORE, NULL, ph_t("Non riesco a creare il file della registrazione"));
        } else {
            g_mutex_lock(&sf->m);
            sf->reg = r;
            g_strlcpy(sf->reg_percorso, perc, sizeof sf->reg_percorso);
            g_mutex_unlock(&sf->m);
            emetti_reg(sf, PH_SRV_REG_INIZIATA, perc, NULL);
            chiave_corpo(sv, sf); /* il file parte dal primo fotogramma chiave */
        }
    }
    free(j->cartella);
    free(j->app);
    g_free(j);
    sf_lascia(sf);
}

void sv_flusso_registra(void *b, void *fl, bool avvia, const char *cartella_pc, const char *nome_app,
                        bool con_audio)
{
    /* Dopo ph_servizi_libera il pezzo comune passa b = NULL: un widget che
     * sopravvive alla chiusura (qualcuno tiene un riferimento) non deve arrivare
     * qui a toccare un backend gia' liberato. I flussi rimasti li ha liberati
     * sv_flussi_chiudi_finale. */
    if (b == NULL)
        return;
    Sv *sv = b;
    (void)con_audio;
    RegJob *j = g_new0(RegJob, 1);
    j->sf = fl;
    j->avvia = avvia;
    j->cartella = strdup(cartella_pc != NULL ? cartella_pc : "");
    j->app = strdup(nome_app != NULL ? nome_app : "");
    if (j->cartella == NULL || j->app == NULL) {
        free(j->cartella);
        free(j->app);
        g_free(j);
        emetti_reg(fl, PH_SRV_REG_ERRORE, NULL, ph_t("Non riesco a creare il file della registrazione"));
        return;
    }
    sf_prendi(j->sf);
    sv_lavoro(sv, sv->p_flussi, reg_lavoro, j);
}

/* ---- Input ------------------------------------------------------------------------------------- */
typedef struct {
    SvFlusso *sf;
    PhSrvInput in;
    char *testo;
    bool pizzico; /* un passo di pizzico (in.video_w/h, x, y; fase e passo qui) */
    int fase, passo;
} InputJob;

static bool solo_ascii(const char *t)
{
    for (; *t != '\0'; t++)
        if ((unsigned char)*t >= 0x80)
            return false;
    return true;
}

void (*sv_prova_input_fatto)(void) = NULL;

static void input_lavoro(Sv *sv, void *dati)
{
    InputJob *j = dati;
    SvFlusso *sf = j->sf;
    SvSessione *s = sv_sessione_usa(sv);
    if (s != NULL) {
        g_mutex_lock(&sf->m);
        /* A flusso chiuso passano solo le fasi che CHIUDONO un gesto (SU e ANNULLA
         * di tocco, FINE e ANNULLA di pizzico) finche' vf esiste: sullo specchio
         * un dito resterebbe "giu'" sul telefono. Le altre si scartano. */
        bool chiude_gesto = j->pizzico ? (j->fase == PH_PIZZICO_FINE || j->fase == PH_PIZZICO_ANNULLA)
                                       : (j->in.tipo == PH_SRV_IN_TOCCO &&
                                          (j->in.azione == PH_SRV_TOCCO_SU || j->in.azione == PH_SRV_TOCCO_ANNULLA));
        bool giusta = sf->sess_id == s->id && sf->vf != NULL && (!atomic_load(&sf->chiuso) || chiude_gesto) &&
                      sf->stato == PH_FL_ATTIVO;
        uint32_t id = sf->id_video;
        g_mutex_unlock(&sf->m);
        if (giusta) {
            const PhSrvInput *in = &j->in;
            PhErrore e = {{0}};
            if (j->pizzico) {
                if (ph_input_pizzico(s->c, id, (uint16_t)in->video_w, (uint16_t)in->video_h, (uint8_t)j->fase,
                                     (uint16_t)in->x, (uint16_t)in->y, (int16_t)j->passo, &e) != 0) {
                    fprintf(stderr, "YASDE 2.3: pizzico non mandato\n"); /* mai le coordinate */
                } else {
                    g_mutex_lock(&sf->m);
                    sf->pizzico_inviato = j->fase != PH_PIZZICO_FINE && j->fase != PH_PIZZICO_ANNULLA;
                    sf->pzi_w = in->video_w;
                    sf->pzi_h = in->video_h;
                    sf->pzi_x = in->x;
                    sf->pzi_y = in->y;
                    g_mutex_unlock(&sf->m);
                }
            } else switch (in->tipo) {
            case PH_SRV_IN_TOCCO:
                if (ph_input_tocco(s->c, id, (uint16_t)in->video_w, (uint16_t)in->video_h, (uint8_t)in->azione,
                                   (uint16_t)in->x, (uint16_t)in->y, (uint8_t)(in->pulsanti & 0xff), &e) == 0) {
                    g_mutex_lock(&sf->m);
                    sf->tocco_inviato = in->azione != PH_SRV_TOCCO_SU && in->azione != PH_SRV_TOCCO_ANNULLA;
                    sf->tc_w = in->video_w;
                    sf->tc_h = in->video_h;
                    sf->tc_x = in->x;
                    sf->tc_y = in->y;
                    g_mutex_unlock(&sf->m);
                }
                break;
            case PH_SRV_IN_ROTELLA:
                (void)ph_input_rotella(s->c, id, (uint16_t)in->video_w, (uint16_t)in->video_h, (uint16_t)in->x,
                                       (uint16_t)in->y, (int16_t)in->orizzontale, (int16_t)in->verticale, &e);
                break;
            case PH_SRV_IN_TASTO:
                (void)ph_input_tasto(s->c, id, in->giu, in->codice, in->meta, &e);
                break;
            case PH_SRV_IN_TESTO:
                /* Il testo non ASCII passa dagli appunti con "incolla" (4.10). */
                if (solo_ascii(j->testo))
                    (void)ph_input_testo(s->c, id, j->testo, &e);
                else
                    (void)ph_dati_appunti_incolla(s->d, id, j->testo, &e);
                break;
            case PH_SRV_IN_INDIETRO:
                (void)ph_input_indietro(s->c, id, &e);
                break;
            }
        }
        sv_sessione_lascia(s);
    }
    free(j->testo);
    /* L'input e' partito (o scartato): la barriera lo conta. */
    g_mutex_lock(&sf->m);
    sf->in_inviati++;
    g_cond_broadcast(&sf->cv);
    g_mutex_unlock(&sf->m);
    if (sv_prova_input_fatto != NULL)
        sv_prova_input_fatto();
    g_free(j);
    sf_lascia(sf);
}

void sv_flusso_input(void *b, void *fl, const PhSrvInput *in)
{
    /* Dopo ph_servizi_libera il pezzo comune passa b = NULL: un widget che
     * sopravvive alla chiusura (qualcuno tiene un riferimento) non deve arrivare
     * qui a toccare un backend gia' liberato. I flussi rimasti li ha liberati
     * sv_flussi_chiudi_finale. */
    if (b == NULL)
        return;
    Sv *sv = b;
    SvFlusso *sf = fl;
    if (atomic_load(&sf->chiuso))
        return;
    bool geometrico = in->tipo == PH_SRV_IN_TOCCO || in->tipo == PH_SRV_IN_ROTELLA;
    g_mutex_lock(&sf->m);
    /* Una sessione in pausa non riceve input (2.2); un evento calcolato su una
     * misura diversa da quella attuale il telefono lo scarterebbe (4.5). */
    bool ok = sf->stato == PH_FL_ATTIVO && !sf->pausa &&
              (!geometrico || (in->video_w == sf->w && in->video_h == sf->h));
    g_mutex_unlock(&sf->m);
    if (!ok)
        return;
    /* Il telefono "in mano": il primo input dal PC rimette il pannello spento. */
    sv_pannello_spegni_se_in_mano(sv);
    /* Un telefono che non riesce a stare dietro non accumula movimenti. */
    bool cadibile = in->tipo == PH_SRV_IN_ROTELLA || (in->tipo == PH_SRV_IN_TOCCO && in->azione == PH_SRV_TOCCO_MUOVI);
    if (cadibile && g_thread_pool_unprocessed(sv->p_input) > CODA_INPUT_MAX)
        return;
    InputJob *j = g_new0(InputJob, 1);
    j->sf = sf;
    j->in = *in;
    if (in->testo != NULL) {
        j->testo = strdup(in->testo);
        if (j->testo == NULL) {
            g_free(j);
            return;
        }
        j->in.testo = j->testo;
    }
    g_mutex_lock(&sf->m);
    sf->in_accodati++;
    g_mutex_unlock(&sf->m);
    sf_prendi(sf);
    sv_lavoro(sv, sv->p_input, input_lavoro, j);
}

/* ---- Pizzico (Ctrl + rotella = zoom, CONTRATTI.md 17) -------------------------------------------
 * Come flusso_input: stessa coda ordinata (p_input), stessa barriera e stessi
 * riferimenti, mai bloccante; nulla in pausa, a flusso perso o in apertura
 * (stato non ATTIVO), a misura diversa o a flusso chiuso. */
static void pizzico_in_coda(Sv *sv, SvFlusso *sf, int w, int h, int fase, int x, int y, int passo)
{
    InputJob *j = g_new0(InputJob, 1);
    j->sf = sf;
    j->pizzico = true;
    j->in.video_w = w;
    j->in.video_h = h;
    j->in.x = x;
    j->in.y = y;
    j->fase = fase;
    j->passo = passo;
    g_mutex_lock(&sf->m);
    sf->in_accodati++;
    g_mutex_unlock(&sf->m);
    sf_prendi(sf);
    sv_lavoro(sv, sv->p_input, input_lavoro, j);
}

/* Se la shell ha un pizzico aperto lo annulla (in coda con gli altri input, che
 * la barriera aspetta). Dal thread GTK, prima di pausa e ridimensionamento. */
static void pizzico_chiudi(Sv *sv, SvFlusso *sf)
{
    g_mutex_lock(&sf->m);
    bool aperto = sf->pizzico_aperto;
    int w = sf->pz_w, h = sf->pz_h, x = sf->pz_x, y = sf->pz_y;
    sf->pizzico_aperto = false;
    g_mutex_unlock(&sf->m);
    if (aperto)
        pizzico_in_coda(sv, sf, w, h, PH_PIZZICO_ANNULLA, x, y, 0);
}

/* Alla chiusura del flusso (lavoro su p_flussi, prima di VIDEO_CHIUDI): gli input
 * in coda si scartano perche' il flusso e' chiuso, quindi l'ANNULLA parte da qui. */
static void pizzico_chiudi_diretto(Sv *sv, SvFlusso *sf)
{
    g_mutex_lock(&sf->m);
    bool aperto = sf->pizzico_inviato; /* cio' che il telefono ha davvero ricevuto */
    int w = sf->pzi_w, h = sf->pzi_h, x = sf->pzi_x, y = sf->pzi_y;
    uint64_t sess = sf->sess_id;
    uint32_t id = sf->id_video;
    sf->pizzico_inviato = false;
    sf->pizzico_aperto = false;
    g_mutex_unlock(&sf->m);
    if (!aperto)
        return;
    SvSessione *s = sv_sessione_usa(sv);
    if (s != NULL) {
        PhErrore e = {{0}};
        if (s->id == sess)
            (void)ph_input_pizzico(s->c, id, (uint16_t)w, (uint16_t)h, PH_PIZZICO_ANNULLA, (uint16_t)x, (uint16_t)y, 0,
                                   &e);
        sv_sessione_lascia(s);
    }
}

/* Come pizzico_chiudi_diretto, per un tocco ancora "giu'" sul telefono. */
static void tocco_chiudi_diretto(Sv *sv, SvFlusso *sf)
{
    g_mutex_lock(&sf->m);
    bool aperto = sf->tocco_inviato;
    int w = sf->tc_w, h = sf->tc_h, x = sf->tc_x, y = sf->tc_y;
    uint64_t sess = sf->sess_id;
    uint32_t id = sf->id_video;
    sf->tocco_inviato = false;
    g_mutex_unlock(&sf->m);
    if (!aperto)
        return;
    SvSessione *s = sv_sessione_usa(sv);
    if (s != NULL) {
        PhErrore e = {{0}};
        if (s->id == sess)
            (void)ph_input_tocco(s->c, id, (uint16_t)w, (uint16_t)h, PH_TOCCO_ANNULLA, (uint16_t)x, (uint16_t)y, 0, &e);
        sv_sessione_lascia(s);
    }
}

void sv_flusso_pizzico(void *b, void *fl, int video_w, int video_h, int fase, int x, int y, int passo)
{
    /* Dopo ph_servizi_libera il pezzo comune passa b = NULL: nulla da toccare. */
    if (b == NULL)
        return;
    Sv *sv = b;
    SvFlusso *sf = fl;
    if (atomic_load(&sf->chiuso))
        return;
    g_mutex_lock(&sf->m);
    bool ok = sf->stato == PH_FL_ATTIVO && !sf->pausa && video_w == sf->w && video_h == sf->h;
    if (ok) {
        if (fase == PH_PIZZICO_INIZIO) {
            sf->pizzico_aperto = true;
            sf->pz_w = video_w;
            sf->pz_h = video_h;
        }
        sf->pz_x = x;
        sf->pz_y = y;
        if (fase == PH_PIZZICO_FINE || fase == PH_PIZZICO_ANNULLA)
            sf->pizzico_aperto = false;
    }
    g_mutex_unlock(&sf->m);
    if (!ok)
        return;
    sv_pannello_spegni_se_in_mano(sv);
    /* Un telefono che non sta dietro non accumula passi (le fasi di chiusura passano sempre). */
    if (fase == PH_PIZZICO_CONTINUA && g_thread_pool_unprocessed(sv->p_input) > CODA_INPUT_MAX)
        return;
    pizzico_in_coda(sv, sf, video_w, video_h, fase, x, y, passo);
}

/* ---- Chiusura ----------------------------------------------------------------------------------- */
typedef struct {
    SvFlusso *sf;
    bool togli_task;
    uint64_t barriera; /* input accodati al momento della chiusura */
} ChiudiJob;

static void flusso_libera(SvFlusso *sf)
{
    free(sf->cfg);
    g_queue_clear(&sf->scatti);
    if (sf->dec != NULL)
        ph_decodifica_libera(sf->dec);
    g_mutex_clear(&sf->m);
    g_cond_clear(&sf->cv);
    g_free(sf);
}

static void chiudi_vf(Sv *sv, SvFlusso *sf, bool togli_task)
{
    g_mutex_lock(&sf->m);
    PhVideoFlusso *vf = sf->vf;
    uint64_t sid = sf->sess_id;
    sf->vf = NULL;
    while (sf->uso > 0)
        g_cond_wait(&sf->cv, &sf->m);
    g_mutex_unlock(&sf->m);
    if (vf == NULL)
        return;
    /* Se la sessione di vf e' ancora quella attuale la si tiene in uso mentre
     * VIDEO_CHIUDI parte; altrimenti il flusso e' di una sessione caduta e
     * basta liberarlo (componente.h). */
    SvSessione *s = sv_sessione_usa(sv);
    ph_video_chiudi(vf, togli_task);
    if (s != NULL) {
        (void)sid;
        sv_sessione_lascia(s);
    }
}

static void chiudi_lavoro(Sv *sv, void *dati)
{
    ChiudiJob *j = dati;
    /* Gli input gia' in coda (SU, FINE...) partono PRIMA di VIDEO_CHIUDI: la
     * barriera li aspetta (al massimo 2 s, anche a flusso chiuso); poi i gesti
     * che la shell crede ancora aperti si annullano. */
    barriera_input_c(j->sf, j->barriera, true);
    pizzico_chiudi_diretto(sv, j->sf);
    tocco_chiudi_diretto(sv, j->sf);
    chiudi_vf(sv, j->sf, j->togli_task);
    /* Il thread di lettura e' finito: registrazione chiusa in silenzio (il file
     * resta completo fino all'ultimo frammento) e screenshot in coda chiusi. */
    reg_stacca_e_chiudi(j->sf, NULL, false);
    scatti_servi(j->sf, NULL, true);
    sf_lascia(j->sf); /* quello del proprietario: libera chi lascia per ultimo */
    g_free(j);
}

void sv_flusso_chiudi(void *b, void *fl, bool togli_task)
{
    /* Dopo ph_servizi_libera il pezzo comune passa b = NULL: un widget che
     * sopravvive alla chiusura (qualcuno tiene un riferimento) non deve arrivare
     * qui a toccare un backend gia' liberato. I flussi rimasti li ha liberati
     * sv_flussi_chiudi_finale. */
    if (b == NULL)
        return;
    Sv *sv = b;
    SvFlusso *sf = fl;
    /* Da qui nessun avviso: sf->f e' della shell e ph_srv_flusso_chiudi lo
     * libera appena torniamo. La coda di p_flussi e' in ordine: i lavori gia'
     * messi in coda per questo flusso girano prima della chiusura. */
    g_mutex_lock(&sv->m);
    g_mutex_lock(&sf->m);
    sf->f = NULL;
    atomic_store(&sf->chiuso, true);
    uint64_t barriera_chiusura = sf->in_accodati;
    g_cond_broadcast(&sf->cv); /* chi aspetta la barriera degli input lo vede */
    g_mutex_unlock(&sf->m);
    g_ptr_array_remove(sv->flussi, sf);
    g_mutex_unlock(&sv->m);
    ChiudiJob *j = g_new0(ChiudiJob, 1);
    j->sf = sf;
    j->togli_task = togli_task;
    j->barriera = barriera_chiusura;
    sv_lavoro(sv, sv->p_flussi, chiudi_lavoro, j);
    /* Chiusa l'ultima finestra (lo specchio conta) il pannello NON si riaccende:
     * regola del committente, vedi sv_dati.c e PIANO.md. */
}

void sv_flussi_chiudi_finale(Sv *sv)
{
    /* La shell chiude i suoi flussi prima dei servizi (servizi_backend.h):
     * se ne resta qualcuno lo si libera qui senza avvisi. */
    for (;;) {
        g_mutex_lock(&sv->m);
        SvFlusso *sf = sv->flussi->len > 0 ? g_ptr_array_index(sv->flussi, 0) : NULL;
        if (sf != NULL)
            g_ptr_array_remove_index(sv->flussi, 0);
        g_mutex_unlock(&sv->m);
        if (sf == NULL)
            break;
        g_mutex_lock(&sf->m);
        sf->f = NULL;
        atomic_store(&sf->chiuso, true);
        g_mutex_unlock(&sf->m);
        pizzico_chiudi_diretto(sv, sf);
        tocco_chiudi_diretto(sv, sf);
        chiudi_vf(sv, sf, false);
        reg_stacca_e_chiudi(sf, NULL, false);
        scatti_servi(sf, NULL, true);
        sf_lascia(sf);
    }
}

/* ---- Eventi e sessioni (gestore, componente) ------------------------------------------------------ */
void sv_ev_video(uint32_t id_video, uint8_t che_cosa, uint16_t a, uint16_t b, void *utente)
{
    Sv *sv = utente;
    uint64_t sid = sv_sessione_id(sv);
    if (sid == 0)
        return;
    g_mutex_lock(&sv->m);
    for (guint i = 0; i < sv->flussi->len; i++) {
        SvFlusso *sf = g_ptr_array_index(sv->flussi, i);
        g_mutex_lock(&sf->m);
        if (sf->vf != NULL && sf->sess_id == sid && sf->id_video == id_video) {
            switch (che_cosa) {
            case 1:
                avviso_semplice(sf, PH_SRV_FL_APP_CHIUSA, sf->w, sf->h, false);
                break;
            case 2:
                avviso_semplice(sf, PH_SRV_FL_PROTETTO, sf->w, sf->h, a != 0);
                break;
            case 3:
                if (a > 0 && b > 0 && (a != sf->w || b != sf->h)) {
                    sf->w = a;
                    sf->h = b;
                    avviso_semplice(sf, PH_SRV_FL_MISURA, a, b, false);
                }
                break;
            case 4:
                passo(sf, PH_FL_EV_CODIFICATORE);
                break;
            default:
                break;
            }
            g_mutex_unlock(&sf->m);
            break;
        }
        g_mutex_unlock(&sf->m);
    }
    g_mutex_unlock(&sv->m);
}

void sv_flussi_sessione_persa(Sv *sv)
{
    g_mutex_lock(&sv->m);
    for (guint i = 0; i < sv->flussi->len; i++) {
        SvFlusso *sf = g_ptr_array_index(sv->flussi, i);
        g_mutex_lock(&sf->m);
        passo(sf, PH_FL_EV_SESSIONE_PERSA);
        sf->pizzico_aperto = false; /* il gesto e' morto con la sessione */
        sf->tocco_inviato = sf->pizzico_inviato = false;
        bool registra = sf->reg != NULL;
        g_mutex_unlock(&sf->m);
        if (registra) /* la connessione persa ferma la registrazione */
            reg_chiedi_stop(sf, ph_t("Registrazione interrotta: connessione persa"));
    }
    g_mutex_unlock(&sv->m);
}

void sv_flussi_sessione_nuova(Sv *sv)
{
    g_mutex_lock(&sv->m);
    for (guint i = 0; i < sv->flussi->len; i++) {
        SvFlusso *sf = g_ptr_array_index(sv->flussi, i);
        g_mutex_lock(&sf->m);
        PhFlPasso p = passo(sf, PH_FL_EV_SESSIONE_NUOVA);
        g_mutex_unlock(&sf->m);
        if (p.riapri) {
            ph_shell_decisione("flusso riaperto", "connessione tornata");
            sf_prendi(sf);
            sv_lavoro(sv, sv->p_flussi, apri_lavoro, sf); /* dentro sv->m: il flusso non sparisce */
        }
    }
    g_mutex_unlock(&sv->m);
}

#endif
