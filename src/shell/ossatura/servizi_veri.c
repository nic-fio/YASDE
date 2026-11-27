/* Nucleo dei servizi veri (servizi_veri.c): collegamento (avvisi, prestiti), gestore delle
 * sessioni, profilo e preferenze, pool di lavoro, chiusura. Il modello e'
 * descritto in sv_interno.h. */
#include <stdio.h>
#include <string.h>

#include "sv_interno.h"

#if PH_OSSATURA_SERVIZI_VERI

/* ---- Vita di Sv (contato: una consegna in volo lo tiene vivo) -------------- */
void sv_rif_prendi(Sv *sv)
{
    atomic_fetch_add(&sv->rif, 1);
}

void sv_rif_lascia(Sv *sv)
{
    if (atomic_fetch_sub(&sv->rif, 1) != 1)
        return;
    g_mutex_clear(&sv->m);
    g_cond_clear(&sv->cv);
    g_mutex_clear(&sv->tm);
    g_mutex_clear(&sv->am);
    g_mutex_clear(&sv->ctl_m);
    g_mutex_clear(&sv->abb_m);
    sv_meteo_libera(sv);
    g_async_queue_unref(sv->coda);
    if (sv->flussi != NULL)
        g_ptr_array_free(sv->flussi, TRUE);
    g_free(sv);
}

/* ---- Pool ------------------------------------------------------------------- */
typedef struct {
    SvFn fn;
    void *dati;
    Sv *sv;
} Lavoro;

static void pool_corpo(gpointer job, gpointer utente)
{
    Lavoro *l = job;
    SvFn fn = l->fn;
    void *dati = l->dati;
    g_free(l);
    fn(utente, dati);
}

static gpointer ripiego_corpo(gpointer p)
{
    Lavoro *l = p;
    pool_corpo(l, l->sv);
    return NULL;
}

void sv_lavoro(Sv *sv, GThreadPool *pool, SvFn fn, void *dati)
{
    Lavoro *l = g_new(Lavoro, 1);
    l->fn = fn;
    l->dati = dati;
    l->sv = sv;
    GError *err = NULL;
    if (!g_thread_pool_push(pool, l, &err)) {
        /* Memoria o thread finiti: il lavoro deve comunque chiudersi (chi lo ha
         * chiesto aspetta una risposta). Si fa su un thread a parte, mai qui:
         * alcuni chiamanti tengono sv->m o sono il thread GTK. */
        g_clear_error(&err);
        GThread *t = g_thread_try_new("ph-lavoro", ripiego_corpo, l, NULL);
        if (t != NULL)
            g_thread_unref(t);
        else
            fprintf(stderr, "YASDE 2.2: lavoro perso: memoria o thread finiti\n");
    }
}

/* ---- Sessione: uso --------------------------------------------------------------- */
SvSessione *sv_sessione_usa(Sv *sv)
{
    SvSessione *r = NULL;
    g_mutex_lock(&sv->m);
    SvSessione *s = sv->sess;
    if (s != NULL) {
        g_mutex_lock(&s->m);
        if (s->attiva) {
            s->uso++;
            r = s;
        }
        g_mutex_unlock(&s->m);
    }
    g_mutex_unlock(&sv->m);
    return r;
}

void sv_sessione_lascia(SvSessione *s)
{
    g_mutex_lock(&s->m);
    s->uso--;
    g_cond_broadcast(&s->cv);
    g_mutex_unlock(&s->m);
}

uint64_t sv_sessione_id(Sv *sv)
{
    return atomic_load(&sv->sess_id);
}

void sv_manager_invia(Sv *sv, int tipo)
{
    g_async_queue_push(sv->coda, GINT_TO_POINTER(tipo));
}

/* ---- Avvisi del collegamento (thread del collegamento: tornare subito) ------- */
static void col_avviso(const PhColAvviso *a, void *utente)
{
    Sv *sv = utente;
    /* Prima si conclude l'abbinamento (se questo avviso ne porta l'esito), POI si pubblica lo
     * stato: quando arriva PH_SRV_EV_COLLEGAMENTO l'abbinamento non e' piu' "in corso"
     * (anche se la richiesta era stata annullata e nessuna risposta arriva). */
    sv_abbina_avviso(sv, a);
    PhSrvCollegamento c;
    ph_ossatura_collegamento_da_avviso(a, &c);
    /* Quanti telefoni sono gia' noti: il collegamento aggiorna telefoni.toml a
     * ogni collegamento riuscito, quindi si rilegge solo nei passaggi a
     * COLLEGATO (e BLOCCATO) e a NON_ABBINATO. */
    if (a->stato == PH_COL_COLLEGATO || a->stato == PH_COL_BLOCCATO || a->stato == PH_COL_NON_ABBINATO) {
        int noti = sv_telefoni_noti(); /* solo ai collegamenti (e quando il telefono non ci conosce) */
        if (noti >= 0)
            atomic_store(&sv->telefoni_noti, noti);
    }
    c.telefoni_noti = atomic_load(&sv->telefoni_noti);
    ph_servizi_pubblica_collegamento(sv->serv, &c);
    if (a->stato == PH_COL_COLLEGATO || a->stato == PH_COL_BLOCCATO)
        sv_manager_invia(sv, SV_MG_SESSIONE);
}

/* "Sto per perdere la connessione" (caduta o chiusura): da qui nessuno usa
 * piu' la sessione. Il resto (chiudere, restituire) lo fa il gestore. */
static void col_in_perdita(void *utente)
{
    Sv *sv = utente;
    g_mutex_lock(&sv->m);
    SvSessione *s = sv->sess;
    if (s != NULL) {
        g_mutex_lock(&s->m);
        s->attiva = false;
        g_mutex_unlock(&s->m);
    }
    atomic_store(&sv->sess_id, 0);
    g_mutex_unlock(&sv->m);
    sv_manager_invia(sv, SV_MG_PERDITA);
}

void sv_riconnetti_ora(void *b)
{
    Sv *sv = b;
    /* Dal thread GTK: sv->col cambia solo in avvio e in chiusura, entrambi
     * sul thread GTK. ph_col_riconnetti_ora non blocca. */
    if (sv->col != NULL && !atomic_load(&sv->chiusura)) {
        PhErrore e = {{0}};
        (void)ph_col_riconnetti_ora(sv->col, &e);
    }
}

/* ---- Persistenza ------------------------------------------------------------------ */
typedef struct {
    char seriale[PH_CFG_LUN_SERIALE];
    PhCfgTelefono *copia;
} SalvaProfilo;

static void salva_profilo_lavoro(Sv *sv, void *dati)
{
    (void)sv;
    SalvaProfilo *j = dati;
    PhErrore e = {{0}};
    if (ph_cfg_telefono_salva(j->seriale, j->copia, &e) != 0)
        fprintf(stderr, "YASDE 2.2: profilo del telefono non salvato\n");
    ph_cfg_telefono_libera(j->copia);
    g_free(j);
}

void sv_profilo_salva(void *b, PhCfgTelefono *copia)
{
    Sv *sv = b;
    /* Senza telefono noto, o con il file illeggibile in modo grave, non si
     * salva (config.h: dopo -1 NON si deve salvare). */
    if (sv->seriale_gtk[0] == '\0' || !sv->profilo_ok_gtk) {
        ph_cfg_telefono_libera(copia);
        return;
    }
    SalvaProfilo *j = g_new0(SalvaProfilo, 1);
    g_strlcpy(j->seriale, sv->seriale_gtk, sizeof j->seriale);
    j->copia = copia;
    sv_lavoro(sv, sv->p_scrittura, salva_profilo_lavoro, j);
}

static void salva_pref_lavoro(Sv *sv, void *dati)
{
    (void)sv;
    PhCfgPreferenze *p = dati;
    PhErrore e = {{0}};
    if (ph_cfg_preferenze_salva(p, &e) != 0)
        fprintf(stderr, "YASDE 2.2: preferenze non salvate\n");
    free(p);
}

void sv_preferenze_salva(void *b, PhCfgPreferenze *copia)
{
    Sv *sv = b;
    if (!sv->pref_ok) {
        free(copia);
        return;
    }
    sv_lavoro(sv, sv->p_scrittura, salva_pref_lavoro, copia);
}

/* Il seriale del profilo cambia sul thread GTK, DOPO che il profilo nuovo e'
 * stato applicato (stessa catena di consegne): cosi' il salvataggio in attesa
 * del profilo vecchio, che parte quando arriva il nuovo, va al file giusto. */
typedef struct {
    Sv *sv;
    char seriale[PH_CFG_LUN_SERIALE];
    bool ok;
} CambioSeriale;

static void cambio_seriale_applica(void *p)
{
    CambioSeriale *c = p;
    if (atomic_load(&c->sv->chiusura) == 0) {
        g_strlcpy(c->sv->seriale_gtk, c->seriale, sizeof c->sv->seriale_gtk);
        c->sv->profilo_ok_gtk = c->ok;
    }
}

static void cambio_seriale_libera(void *p)
{
    CambioSeriale *c = p;
    sv_rif_lascia(c->sv);
    g_free(c);
}

/* Carica il profilo del telefono della sessione e lo pubblica (gestore). */
static void profilo_carica(Sv *sv, const char *seriale)
{
    PhCfgTelefono *prof = ph_cfg_telefono_nuovo();
    if (prof == NULL)
        return;
    bool ok = false;
    if (seriale[0] != '\0') {
        PhErrore e = {{0}};
        int r = ph_cfg_telefono_carica(seriale, prof, &e);
        ok = r != -1;
        if (r == -1)
            ph_cfg_telefono_predefinito(prof); /* e per questa sessione mai salvare */
        if (r != 0)
            fprintf(stderr, "YASDE 2.2: profilo del telefono %s\n",
                    r == 1 ? "illeggibile, messo da parte" : "non letto, uso i valori predefiniti");
    }
    int volume = prof->volume;
    ph_servizi_pubblica_profilo(sv->serv, prof); /* prende la proprieta' */
    CambioSeriale *c = g_new0(CambioSeriale, 1);
    sv_rif_prendi(sv);
    c->sv = sv;
    g_strlcpy(c->seriale, seriale, sizeof c->seriale);
    c->ok = ok;
    ph_shell_consegna(cambio_seriale_applica, c, cambio_seriale_libera);
    sv_audio_volume_iniziale(sv, volume);
}

/* ---- Gestore -------------------------------------------------------------------------- */
/* solo_se_morta: l'evento di caduta non dice di quale sessione parla; se la
 * sessione attuale e' ancora buona (e' una nuova, nata dopo) non si tocca. */
static void sessione_smonta(Sv *sv, bool solo_se_morta)
{
    g_mutex_lock(&sv->m);
    SvSessione *s = sv->sess;
    if (s != NULL && solo_se_morta) {
        g_mutex_lock(&s->m);
        bool viva = s->attiva && ph_col_prestito_attivo(s->p);
        g_mutex_unlock(&s->m);
        if (viva) {
            g_mutex_unlock(&sv->m);
            return;
        }
    }
    sv->sess = NULL;
    if (s != NULL) {
        g_mutex_lock(&s->m);
        s->attiva = false;
        g_mutex_unlock(&s->m);
    }
    atomic_store(&sv->sess_id, 0);
    g_mutex_unlock(&sv->m);
    if (s == NULL)
        return;
    bool chiusura = atomic_load(&sv->chiusura) != 0;
    if (!chiusura)
        sv_meteo_sessione(sv); /* senza telefono: la posizione non c'e' piu' */

    sv_flussi_sessione_persa(sv);
    /* Chi sta usando la sessione finisce (ogni uso e' limitato dai tempi dei
     * pezzi sotto o dall'annullamento), poi si chiude cio' che era aperto. */
    g_mutex_lock(&s->m);
    while (s->uso > 0)
        g_cond_wait(&s->cv, &s->m);
    g_mutex_unlock(&s->m);
    sv_dati_teardown(sv, s);
    sv_file_teardown(s);
    sv_audio_teardown(sv);
    sv_tr_teardown(sv, s, chiusura);
    ph_col_restituisci(s->p);
    g_mutex_clear(&s->m);
    g_cond_clear(&s->cv);
    g_free(s);
}

static void sessione_nuova(Sv *sv)
{
    if (atomic_load(&sv->chiusura))
        return;
    g_mutex_lock(&sv->m);
    SvSessione *vecchia = sv->sess;
    bool buona = vecchia != NULL && ph_col_prestito_attivo(vecchia->p);
    g_mutex_unlock(&sv->m);
    if (buona)
        return; /* BLOCCATO <-> COLLEGATO: e' la stessa sessione */
    if (vecchia != NULL)
        sessione_smonta(sv, false);

    g_mutex_lock(&sv->m);
    PhCollegamento *col = (!atomic_load(&sv->chiusura) && sv->col != NULL) ? sv->col : NULL;
    if (col != NULL)
        sv->col_uso++;
    g_mutex_unlock(&sv->m);
    if (col == NULL)
        return;
    PhErrore e = {{0}};
    PhColPrestito *p = ph_col_presta(col, &e);
    g_mutex_lock(&sv->m);
    sv->col_uso--;
    g_cond_broadcast(&sv->cv);
    g_mutex_unlock(&sv->m);
    if (p == NULL)
        return; /* la sessione e' gia' caduta: arrivera' un altro avviso */

    SvSessione *s = g_new0(SvSessione, 1);
    g_mutex_init(&s->m);
    g_cond_init(&s->cv);
    s->id = ++sv->prossima_sessione;
    s->p = p;
    s->c = ph_col_prestito_componente(p);
    s->d = ph_col_prestito_dati(p);
    s->a = ph_col_prestito_adb(p);
    const PhColTelefono *t = ph_col_prestito_telefono(p);
    char seriale[PH_CFG_LUN_SERIALE];
    g_strlcpy(seriale, t->seriale, sizeof seriale);
    s->schermo_w = t->info.larghezza;
    s->schermo_h = t->info.altezza;
    s->densita = t->info.densita;
    atomic_init(&s->prima_notifiche, true);
    /* A ogni nuova connessione, prima di qualunque installazione: via i
     * temporanei di APK rimasti sul telefono (installa.h). La sessione non e'
     * ancora pubblicata, quindi nessuna installazione e' in corso. */
    (void)ph_inst_pulisci(s->a, &e);

    /* Pubblicata solo se la sessione e' ancora buona, con il lucchetto preso:
     * una in_perdita che arriva dopo trova la sessione e la marca. */
    g_mutex_lock(&sv->m);
    bool pubblicata = ph_col_prestito_attivo(p);
    if (pubblicata) {
        g_mutex_lock(&s->m);
        s->attiva = true;
        g_mutex_unlock(&s->m);
        sv->sess = s;
        atomic_store(&sv->sess_id, s->id);
    }
    g_mutex_unlock(&sv->m);
    if (!pubblicata) { /* caduta durante la preparazione: si restituisce subito */
        ph_col_restituisci(p);
        g_mutex_clear(&s->m);
        g_cond_clear(&s->cv);
        g_free(s);
        return;
    }

    /* Costruzione: se cade durante, in_perdita ha gia' marcato la sessione e
     * arrivera' SV_MG_PERDITA a smontarla; le chiamate falliscono in fretta. */
    /* Prima del profilo: il desktop rimanda la citta' scelta appena lo riceve. */
    sv_meteo_telefono(sv, seriale);
    profilo_carica(sv, seriale);
    if (!atomic_load(&sv->chiusura))
        sv_dati_sessione_nuova(sv, s);
    if (!atomic_load(&sv->chiusura))
        sv_tr_sessione_nuova(sv, s);
    if (!atomic_load(&sv->chiusura))
        sv_flussi_sessione_nuova(sv);
    if (!atomic_load(&sv->chiusura))
        sv_meteo_sessione(sv); /* c'e' un telefono: la posizione e' sua */
}

void (*sv_prova_evento_gestito)(void) = NULL;
Sv *sv_prova_sv = NULL;

static gpointer manager_corpo(gpointer utente)
{
    Sv *sv = utente;
    g_mutex_lock(&sv->m);
    while (!sv->avvio_finito)
        g_cond_wait(&sv->cv, &sv->m);
    g_mutex_unlock(&sv->m);
    for (;;) {
        gint64 attesa = sv_audio_attesa_us(sv);
        gpointer ev = attesa < 0 ? g_async_queue_pop(sv->coda)
                                 : g_async_queue_timeout_pop(sv->coda, (guint64)attesa);
        if (ev == NULL) { /* scaduta l'attesa dell'audio */
            sv_audio_avvia_se_dovuto(sv);
            continue;
        }
        switch (GPOINTER_TO_INT(ev)) {
        case SV_MG_SESSIONE:
            sessione_nuova(sv);
            break;
        case SV_MG_PERDITA:
            sessione_smonta(sv, true);
            break;
        case SV_MG_AUDIO:
            sv_audio_avvia_se_dovuto(sv);
            break;
        case SV_MG_FINE:
            return NULL;
        }
        if (sv_prova_evento_gestito != NULL)
            sv_prova_evento_gestito();
    }
}

/* ---- Fine ------------------------------------------------------------------------------ */
static void sv_chiudi(void *b)
{
    Sv *sv = b;
    atomic_store(&sv->chiusura, 1);
    /* Il meteo per primo: ferma timer e richieste e chiude PhMeteo, cosi' nessuna
     * sua callback puo' arrivare mentre il resto si chiude. */
    sv_meteo_chiudi(sv);
    sv_abbina_chiudi(sv); /* un abbinamento in corso si chiude: nulla resta appeso */
    /* Nessuna nuova ph_col_presta del gestore, e aspettiamo quelle in corso:
     * poi il collegamento si puo' chiudere senza che nessuno lo usi. */
    g_mutex_lock(&sv->m);
    while (sv->col_uso > 0)
        g_cond_wait(&sv->cv, &sv->m);
    PhCollegamento *col = sv->col;
    g_mutex_unlock(&sv->m);
    if (col != NULL) {
        /* in_perdita -> SV_MG_PERDITA: il gestore smonta la sessione e
         * restituisce il prestito mentre ph_col_chiudi lo aspetta. */
        int r = ph_col_chiudi(col);
        if (r == 1)
            fprintf(stderr, "YASDE 2.2: un prestito non e' tornato in tempo alla chiusura\n");
        g_mutex_lock(&sv->m);
        sv->col = NULL;
        g_mutex_unlock(&sv->m);
    }
    if (sv->manager != NULL) {
        sv_manager_invia(sv, SV_MG_FINE);
        g_thread_join(sv->manager);
        sv->manager = NULL;
    }
    /* Il gestore e' fermo: nessuno mette piu' lavori in coda (le funzioni
     * della tabella arrivano dal thread GTK, che e' qui). Ogni lavoro in
     * coda si chiude da se' (sessione assente = NON_COLLEGATO). */
    GThreadPool **pool[] = {&sv->p_input, &sv->p_installa, &sv->p_flussi, &sv->p_lavori, &sv->p_stato, &sv->p_icone,
                            &sv->p_scrittura};
    for (size_t i = 0; i < sizeof pool / sizeof pool[0]; i++) {
        if (*pool[i] != NULL) {
            g_thread_pool_free(*pool[i], FALSE, TRUE);
            *pool[i] = NULL;
        }
    }
    sv_flussi_chiudi_finale(sv);
    sv_tr_chiudi_finale(sv);
    sv_rif_lascia(sv);
}

/* ---- Creazione ------------------------------------------------------------------------ */
static GThreadPool *pool_nuovo(Sv *sv, int thread)
{
    return g_thread_pool_new(pool_corpo, sv, thread, FALSE, NULL);
}

/* Il jar sta accanto all'eseguibile. */
static bool percorso_jar(char *out, size_t cap)
{
    char *exe = g_file_read_link("/proc/self/exe", NULL);
    if (exe == NULL)
        return false;
    char *dir = g_path_get_dirname(exe);
    int n = g_snprintf(out, cap, "%s/yasde.jar", dir);
    g_free(dir);
    g_free(exe);
    return n > 0 && (size_t)n < cap;
}

PhServizi *ph_ossatura_servizi_veri(const PhCfgPreferenze *pref, bool pref_salvabile, PhErrore *e)
{
    Sv *sv = g_new0(Sv, 1);
    sv_prova_sv = sv;
    atomic_init(&sv->rif, 1);
    atomic_init(&sv->chiusura, 0);
    atomic_init(&sv->sess_id, 0);
    atomic_init(&sv->decod_sw_sess, 0);
    atomic_init(&sv->tr, NULL);
    atomic_init(&sv->notifiche_in_coda, 0);
    atomic_init(&sv->trova_attivo, false);
    atomic_init(&sv->telefoni_noti, 0);
    g_mutex_init(&sv->abb_m);
    atomic_init(&sv->pannello_spegnimento, 0);
    atomic_init(&sv->pannello_non_prima_us, 0);
    atomic_init(&sv->cache_n, 0);
    g_mutex_init(&sv->m);
    g_cond_init(&sv->cv);
    g_mutex_init(&sv->tm);
    g_mutex_init(&sv->am);
    g_mutex_init(&sv->ctl_m);
    sv_meteo_inizia(sv);
    sv->coda = g_async_queue_new();
    sv->flussi = g_ptr_array_new();
    sv->pref_ok = pref_salvabile;
    sv->vol.volume = 100; /* come il profilo predefinito (config.c) */

    if (ph_cfg_percorso_chiave(sv->chiave, sizeof sv->chiave, e) != 0 ||
        !percorso_jar(sv->jar, sizeof sv->jar)) {
        if (sv->chiave[0] != '\0')
            ph_errore(e, "non trovo il componente per il telefono accanto al programma");
        sv_rif_lascia(sv);
        return NULL;
    }

    PhServiziBackend t = {
        .riconnetti_ora = sv_riconnetti_ora,
        .pannello = sv_pannello,
        .volume = sv_volume,
        .muto = sv_muto,
        .appunti_imposta = sv_appunti_imposta,
        .icona_chiedi = sv_icona_chiedi,
        .tel_elenca = sv_tel_elenca,
        .tel_cerca = sv_tel_cerca,
        .tel_crea_cartella = sv_tel_crea_cartella,
        .tel_miniatura = sv_tel_miniatura,
        .tel_leggi = sv_tel_leggi,
        .tel_memoria = sv_tel_memoria,
        .tr_accoda = sv_tr_accoda,
        .tr_annulla = sv_tr_annulla,
        .tr_annulla_tutto = sv_tr_annulla_tutto,
        .tr_rimuovi_finiti = sv_tr_rimuovi_finiti,
        .profilo_salva = sv_profilo_salva,
        .preferenze_salva = sv_preferenze_salva,
        .flusso_apri = sv_flusso_apri,
        .flusso_leggi = sv_flusso_leggi,
        .flusso_interrompi = sv_flusso_interrompi,
        .flusso_memoria = sv_flusso_memoria,
        .flusso_pausa = sv_flusso_pausa,
        .flusso_ridimensiona = sv_flusso_ridimensiona,
        .flusso_ridimensiona_densita = sv_flusso_ridimensiona_densita,
        .flusso_input = sv_flusso_input,
        .flusso_chiudi = sv_flusso_chiudi,
        .chiudi = sv_chiudi,
        .apk_installa = sv_apk_installa,
        .app_disinstalla = sv_app_disinstalla,
        .tr_annulla_verso = sv_tr_annulla_verso,
        .pannello_chiedi = sv_pannello_chiedi,
        .tel_memoria_dettaglio = sv_tel_memoria_dettaglio,
        .tel_calendario = sv_tel_calendario,
        .flusso_chiave = sv_flusso_chiave,
        .flusso_pagina = sv_flusso_pagina,
        .abbina = sv_abbina,
        .flusso_pizzico = sv_flusso_pizzico,
        .tel_calendario_intervallo = sv_tel_calendario_intervallo,
        .tel_sveglia = sv_tel_sveglia,
        .tel_batteria = sv_tel_batteria,
        .controlli_osserva = sv_controlli_osserva,
        .controllo_imposta = sv_controllo_imposta,
        .trova_avvia = sv_trova_avvia,
        .trova_ferma = sv_trova_ferma,
        .flusso_screenshot = sv_flusso_screenshot,
        .flusso_registra = sv_flusso_registra,
        .meteo_osserva = sv_meteo_osserva,
        .meteo_localita = sv_meteo_localita,
        .meteo_cerca = sv_meteo_cerca,
    };
    sv->serv = ph_servizi_nuovi(&t, sv);
    if (sv->serv == NULL) {
        ph_errore(e, "memoria finita");
        sv_rif_lascia(sv);
        return NULL;
    }
    PhServizi *serv = sv->serv;

    sv->p_lavori = pool_nuovo(sv, 3);
    sv->p_stato = pool_nuovo(sv, 1);
    sv->p_icone = pool_nuovo(sv, 1);
    sv->p_scrittura = pool_nuovo(sv, 1);
    sv->p_flussi = pool_nuovo(sv, 1);
    sv->p_input = pool_nuovo(sv, 1);
    sv->p_installa = pool_nuovo(sv, 1);
    sv->manager = g_thread_new("ph-gestore", manager_corpo, sv);

    /* Le preferenze lette all'avvio. */
    ph_servizi_pubblica_preferenze(serv, pref);

    PhColOpzioni o;
    ph_col_opzioni_predefinite(&o);
    o.jar_locale = sv->jar;
    o.percorso_chiave = sv->chiave;
    o.avviso = col_avviso;
    o.in_perdita = col_in_perdita;
    o.utente = sv;
    o.evento_video = sv_ev_video;
    o.utente_video = sv;
    o.notifiche = sv_ev_notifiche;
    o.utente_notifiche = sv;
    o.stato_telefono = sv_ev_stato;
    o.utente_stato_telefono = sv;
    PhCollegamento *col = ph_col_avvia(&o, e);

    g_mutex_lock(&sv->m);
    sv->col = col;
    sv->avvio_finito = true;
    g_cond_broadcast(&sv->cv);
    g_mutex_unlock(&sv->m);
    if (col == NULL) {
        ph_servizi_libera(serv);
        return NULL;
    }
    return serv;
}

#else /* !PH_OSSATURA_SERVIZI_VERI */

PhServizi *ph_ossatura_servizi_veri(const PhCfgPreferenze *pref, bool pref_salvabile, PhErrore *e)
{
    (void)pref;
    (void)pref_salvabile;
    ph_errore(e, "servizi veri non disponibili in questa compilazione (mancano i pezzi sotto)");
    return NULL;
}

#endif
