/* Servizi veri: installazione e disinstallazione di app (5.9.1, 5.4), annullo
 * dei trasferimenti di un verso, pannello fisico con esito (4.7), dati dei
 * widget (memoria per categoria, calendario) e fotogramma chiave a richiesta.
 * Tutto su pool; ogni lavoro si chiude con ESATTAMENTE una chiamata finale.
 * I titoli degli eventi del calendario e i nomi dei file non finiscono in
 * nessun log, nemmeno nei messaggi d'errore. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sv_interno.h"

#if PH_OSSATURA_SERVIZI_VERI

/* ---- Installazione --------------------------------------------------------------- */
typedef struct {
    PhSrvLavoro *l;
    char *testo; /* APK o pacchetto */
} InstJob;

static InstJob *inst_nuovo(PhSrvLavoro *l, const char *testo)
{
    InstJob *j = calloc(1, sizeof *j);
    if (j != NULL) {
        j->l = l;
        j->testo = strdup(testo);
        if (j->testo == NULL) {
            free(j);
            return NULL;
        }
    }
    return j;
}

static void inst_chiudi_errore(PhSrvLavoro *l, const char *messaggio)
{
    PhSrvInstRisultato r = {.esito = PH_SRV_INST_ERRORE, .messaggio = messaggio, .codice = "",
                            .temporaneo_rimasto = false};
    ph_servizi_lavoro_inst(l, &r);
}

static PhSrvInstEsito esito_inst(PhInstEsito e)
{
    switch (e) {
    case PH_INST_RIUSCITO: return PH_SRV_INST_RIUSCITO;
    case PH_INST_RIFIUTATO: return PH_SRV_INST_RIFIUTATO;
    case PH_INST_ANNULLATO: return PH_SRV_INST_ANNULLATO;
    case PH_INST_ERRORE: break;
    }
    return PH_SRV_INST_ERRORE;
}

static void inst_progresso(uint64_t fatti, uint64_t totale, void *u)
{
    ph_servizi_lavoro_avanzamento(u, fatti, totale);
}

static void inst_chiudi_risultato(PhSrvLavoro *l, PhInstEsito es, const PhInstRisultato *ri)
{
    PhSrvInstRisultato r = {.esito = esito_inst(es), .messaggio = ri->messaggio, .codice = ri->codice,
                            .temporaneo_rimasto = ri->temporaneo_rimasto};
    ph_servizi_lavoro_inst(l, &r);
}

static void installa_lavoro(Sv *sv, void *dati)
{
    InstJob *j = dati;
    SvSessione *s = sv_sessione_usa(sv);
    if (s == NULL) {
        inst_chiudi_errore(j->l, ph_t("Nessun telefono collegato"));
    } else {
        PhInstRisultato ri;
        memset(&ri, 0, sizeof ri);
        PhErrore e = {{0}};
        /* Il flag d'annullamento lo alza chi ha chiesto o la chiusura dei servizi. */
        PhInstEsito es = ph_inst_installa(s->a, j->testo, inst_progresso, j->l, ph_servizi_lavoro_flag_annulla(j->l),
                                          &ri, &e);
        sv_sessione_lascia(s);
        inst_chiudi_risultato(j->l, es, &ri);
    }
    free(j->testo);
    free(j);
}

static void disinstalla_lavoro(Sv *sv, void *dati)
{
    InstJob *j = dati;
    SvSessione *s = sv_sessione_usa(sv);
    if (s == NULL) {
        inst_chiudi_errore(j->l, ph_t("Nessun telefono collegato"));
    } else {
        PhInstRisultato ri;
        memset(&ri, 0, sizeof ri);
        PhErrore e = {{0}};
        PhInstEsito es = ph_inst_disinstalla(s->a, j->testo, &ri, &e);
        sv_sessione_lascia(s);
        inst_chiudi_risultato(j->l, es, &ri);
    }
    free(j->testo);
    free(j);
}

void sv_apk_installa(void *b, PhSrvLavoro *l, const char *apk_pc)
{
    Sv *sv = b;
    InstJob *j = inst_nuovo(l, apk_pc);
    if (j == NULL) {
        inst_chiudi_errore(l, ph_t("Nessun telefono collegato"));
        return;
    }
    sv_lavoro(sv, sv->p_installa, installa_lavoro, j);
}

void sv_app_disinstalla(void *b, PhSrvLavoro *l, const char *pacchetto)
{
    Sv *sv = b;
    InstJob *j = inst_nuovo(l, pacchetto);
    if (j == NULL) {
        inst_chiudi_errore(l, ph_t("Nessun telefono collegato"));
        return;
    }
    sv_lavoro(sv, sv->p_installa, disinstalla_lavoro, j);
}

/* ---- Trasferimenti: annullare un verso ------------------------------------------------ */
void sv_tr_annulla_verso(void *b, PhSrvVerso verso)
{
    Sv *sv = b;
    PhTrasferimenti *t = atomic_load(&sv->tr);
    if (t == NULL)
        return;
    PhTrElenco el = {NULL, 0};
    PhErrore e = {{0}};
    if (ph_tr_elenca(t, &el, &e) != 0)
        return;
    PhTrVerso v = verso == PH_SRV_INVIO ? PH_TR_INVIO : PH_TR_RICEZIONE;
    for (size_t i = 0; i < el.numero; i++)
        if (el.voci[i].verso == v && (el.voci[i].stato == PH_TR_ATTESA || el.voci[i].stato == PH_TR_CORSO))
            (void)ph_tr_annulla(t, el.voci[i].id, NULL);
    ph_tr_elenco_libera(&el);
    /* Annullare non genera sempre avvisi: si rilegge e si pubblica. */
    sv_tr_pubblica(sv);
}

/* ---- Pannello con esito ---------------------------------------------------------------- */
typedef struct {
    PhSrvLavoro *l;
    bool acceso;
} PanJob;

static void pannello_chiedi_lavoro(Sv *sv, void *dati)
{
    PanJob *j = dati;
    SvSessione *s = sv_sessione_usa(sv);
    if (s == NULL) {
        ph_servizi_lavoro_pannello(j->l, PH_SRV_PAN_NON_COLLEGATO, NULL);
    } else {
        uint8_t corpo = j->acceso ? 1 : 0;
        PhErrore e = {{0}};
        int r = ph_componente_richiesta(s->c, 0x60, &corpo, 1, NULL, &e);
        if (r == 0) {
            g_mutex_lock(&sv->tm);
            sv->tel.pannello_noto = true;
            sv->tel.pannello_acceso = j->acceso;
            sv->tel.in_mano = false;
            PhSrvTelefono t = sv->tel;
            g_mutex_unlock(&sv->tm);
            ph_servizi_pubblica_telefono(sv->serv, &t);
        }
        /* Lo stato del pannello e' gia' stato pubblicato, prima dell'esito. */
        sv_sessione_lascia(s);
        PhSrvPannelloEsito es = r == 0 ? PH_SRV_PAN_FATTO : ph_ossatura_esito_pannello(e.testo);
        /* La frase del telefono solo per "altro": i motivi noti hanno il loro testo. */
        if (es == PH_SRV_PAN_ERRORE)
            fprintf(stderr, "ossatura: pannello del telefono: %s\n", e.testo); /* dettaglio solo nel log */
        ph_servizi_lavoro_pannello(j->l, es,
                                   es == PH_SRV_PAN_ERRORE ? ph_t("Non riesco a cambiare lo stato del pannello del telefono.")
                                                           : NULL);
    }
    g_free(j);
}

void sv_pannello_chiedi(void *b, PhSrvLavoro *l, bool acceso)
{
    Sv *sv = b;
    PanJob *j = g_new0(PanJob, 1);
    j->l = l;
    j->acceso = acceso;
    sv_lavoro(sv, sv->p_stato, pannello_chiedi_lavoro, j);
}

/* ---- Widget: memoria per categoria e calendario ------------------------------------------ */
typedef struct {
    PhSrvLavoro *l;
    unsigned massimo;
} WidJob;

static void dettaglio_lavoro(Sv *sv, void *dati)
{
    WidJob *j = dati;
    SvSessione *s = sv_sessione_usa(sv);
    if (s == NULL) {
        ph_servizi_lavoro_dettaglio(j->l, PH_SRV_ESITO_NON_COLLEGATO, NULL, NULL);
    } else {
        PhDatiMemoriaDettaglio d;
        memset(&d, 0, sizeof d);
        PhErrore e = {{0}};
        int r = ph_dati_memoria_dettaglio(s->d, &d, &e);
        sv_sessione_lascia(s);
        PhSrvMemoriaDettaglio o = {
            .immagini = {d.immagini.byte, d.immagini.leggibile}, .video = {d.video.byte, d.video.leggibile},
            .audio = {d.audio.byte, d.audio.leggibile}, .app = {d.app.byte, d.app.leggibile}};
        ph_servizi_lavoro_dettaglio(j->l, r == 0 ? PH_SRV_ESITO_OK : PH_SRV_ESITO_ERRORE, NULL, r == 0 ? &o : NULL);
    }
    g_free(j);
}

void sv_tel_memoria_dettaglio(void *b, PhSrvLavoro *l)
{
    Sv *sv = b;
    WidJob *j = g_new0(WidJob, 1);
    j->l = l;
    sv_lavoro(sv, sv->p_lavori, dettaglio_lavoro, j);
}

static void calendario_lavoro(Sv *sv, void *dati)
{
    WidJob *j = dati;
    SvSessione *s = sv_sessione_usa(sv);
    if (s == NULL) {
        ph_servizi_lavoro_calendario(j->l, PH_SRV_ESITO_NON_COLLEGATO, NULL, NULL, 0);
    } else {
        PhDatiEvento *ev = NULL;
        size_t n = 0;
        PhErrore e = {{0}};
        unsigned massimo = j->massimo < 1 ? 1 : (j->massimo > PH_DATI_CALENDARIO_MAX ? PH_DATI_CALENDARIO_MAX : j->massimo);
        int r = ph_dati_calendario(s->d, (uint8_t)massimo, &ev, &n, &e);
        sv_sessione_lascia(s);
        if (r != 0) {
            /* Mai un elenco vuoto finto; a schermo una frase nostra, niente nei log
             * (il testo del telefono potrebbe contenere titoli di eventi). */
            ph_servizi_lavoro_calendario(j->l, PH_SRV_ESITO_ERRORE, ph_t("Non riesco a leggere il calendario del telefono."),
                                         NULL, 0);
        } else {
            PhSrvEventoCalendario *v = n > 0 ? calloc(n, sizeof *v) : NULL;
            if (n > 0 && v == NULL) {
                ph_dati_calendario_libera(ev, n);
                ph_servizi_lavoro_calendario(j->l, PH_SRV_ESITO_ERRORE, NULL, NULL, 0);
            } else {
                for (size_t i = 0; i < n; i++)
                    v[i] = (PhSrvEventoCalendario){.titolo = ev[i].titolo, .inizio_ms = ev[i].inizio_ms,
                                                   .fine_ms = ev[i].fine_ms, .tutto_il_giorno = ev[i].tutto_il_giorno};
                /* Il pezzo comune copia e ripulisce i titoli per lo schermo. */
                ph_servizi_lavoro_calendario(j->l, PH_SRV_ESITO_OK, NULL, v, n);
                free(v);
                ph_dati_calendario_libera(ev, n);
            }
        }
    }
    g_free(j);
}

void sv_tel_calendario(void *b, PhSrvLavoro *l, unsigned massimo)
{
    Sv *sv = b;
    WidJob *j = g_new0(WidJob, 1);
    j->l = l;
    j->massimo = massimo;
    sv_lavoro(sv, sv->p_lavori, calendario_lavoro, j);
}


/* ---- Widget del 23 nov pomeriggio: calendario del mese, sveglia, batteria,
 * interruttori rapidi, "trova il telefono" -------------------------------------
 * Ogni lavoro: sessione assente -> NON_COLLEGATO; annullato prima di partire ->
 * non si chiede nulla; errore del telefono -> ERRORE con la sua frase (mai un
 * valore inventato: un dato non disponibile arriva come tale). I titoli del
 * calendario non vanno in nessun log. */
typedef struct {
    PhSrvLavoro *l;
    uint64_t da, a;
    int secondi;
    PhSrvControllo ctl;
    int valore;
} NuovoJob;

static NuovoJob *nuovo_job(PhSrvLavoro *l)
{
    NuovoJob *j = g_new0(NuovoJob, 1);
    j->l = l;
    return j;
}

static void cal_intervallo_lavoro(Sv *sv, void *dati)
{
    NuovoJob *j = dati;
    SvSessione *s = sv_sessione_usa(sv);
    if (s == NULL) {
        ph_servizi_lavoro_calendario_intervallo(j->l, PH_SRV_ESITO_NON_COLLEGATO, NULL, NULL, 0, false, "");
    } else if (ph_servizi_lavoro_annullato(j->l)) {
        sv_sessione_lascia(s);
        ph_servizi_lavoro_calendario_intervallo(j->l, PH_SRV_ESITO_ERRORE, NULL, NULL, 0, false, "");
    } else {
        PhDatiCalendarioIntervallo c;
        memset(&c, 0, sizeof c);
        PhErrore e = {{0}};
        int r = ph_dati_calendario_intervallo(s->d, j->da, j->a, PH_DATI_CALENDARIO_INTERVALLO_MAX, &c, &e);
        sv_sessione_lascia(s);
        if (r != 0) {
            ph_servizi_lavoro_calendario_intervallo(j->l, PH_SRV_ESITO_ERRORE,
                                                    ph_t("Non riesco a leggere il calendario del telefono."), NULL, 0, false,
                                                    "");
        } else {
            PhSrvEventoCalendario *v = c.numero > 0 ? calloc(c.numero, sizeof *v) : NULL;
            if (c.numero > 0 && v == NULL) {
                ph_servizi_lavoro_calendario_intervallo(j->l, PH_SRV_ESITO_ERRORE, NULL, NULL, 0, false, "");
            } else {
                for (size_t i = 0; i < c.numero; i++)
                    v[i] = (PhSrvEventoCalendario){.titolo = c.eventi[i].titolo, .inizio_ms = c.eventi[i].inizio_ms,
                                                   .fine_ms = c.eventi[i].fine_ms,
                                                   .tutto_il_giorno = c.eventi[i].tutto_il_giorno};
                /* Il pezzo comune copia e ripulisce i titoli per lo schermo. */
                ph_servizi_lavoro_calendario_intervallo(j->l, PH_SRV_ESITO_OK, NULL, v, c.numero, c.troncato,
                                                        c.app_calendario != NULL ? c.app_calendario : "");
                free(v);
            }
            ph_dati_calendario_intervallo_libera(&c);
        }
    }
    g_free(j);
}

void sv_tel_calendario_intervallo(void *b, PhSrvLavoro *l, uint64_t da_ms, uint64_t a_ms)
{
    if (b == NULL) { /* dopo ph_servizi_libera: nessun backend */
        ph_servizi_lavoro_calendario_intervallo(l, PH_SRV_ESITO_ERRORE, NULL, NULL, 0, false, "");
        return;
    }
    Sv *sv = b;
    NuovoJob *j = nuovo_job(l);
    j->da = da_ms;
    j->a = a_ms;
    sv_lavoro(sv, sv->p_lavori, cal_intervallo_lavoro, j);
}

static void sveglia_lavoro(Sv *sv, void *dati)
{
    NuovoJob *j = dati;
    SvSessione *s = sv_sessione_usa(sv);
    if (s == NULL) {
        ph_servizi_lavoro_sveglia(j->l, PH_SRV_ESITO_NON_COLLEGATO, NULL, false, 0, "");
    } else if (ph_servizi_lavoro_annullato(j->l)) {
        sv_sessione_lascia(s);
        ph_servizi_lavoro_sveglia(j->l, PH_SRV_ESITO_ERRORE, NULL, false, 0, "");
    } else {
        PhDatiSveglia d;
        memset(&d, 0, sizeof d);
        PhErrore e = {{0}};
        int r = ph_dati_sveglia(s->d, &d, &e);
        sv_sessione_lascia(s);
        if (r != 0) {
            fprintf(stderr, "ossatura: sveglia del telefono: %s\n", e.testo);
            ph_servizi_lavoro_sveglia(j->l, PH_SRV_ESITO_ERRORE, ph_t("Non riesco a leggere la sveglia del telefono."), false,
                                      0, "");
        } else {
            ph_servizi_lavoro_sveglia(j->l, PH_SRV_ESITO_OK, NULL, d.presente, d.presente ? d.ora_ms : 0,
                                      d.pacchetto != NULL ? d.pacchetto : "");
            ph_dati_sveglia_libera(&d);
        }
    }
    g_free(j);
}

void sv_tel_sveglia(void *b, PhSrvLavoro *l)
{
    if (b == NULL) {
        ph_servizi_lavoro_sveglia(l, PH_SRV_ESITO_ERRORE, NULL, false, 0, "");
        return;
    }
    Sv *sv = b;
    sv_lavoro(sv, sv->p_lavori, sveglia_lavoro, nuovo_job(l));
}

static void batteria_lavoro(Sv *sv, void *dati)
{
    NuovoJob *j = dati;
    SvSessione *s = sv_sessione_usa(sv);
    if (s == NULL) {
        ph_servizi_lavoro_batteria(j->l, PH_SRV_ESITO_NON_COLLEGATO, NULL, NULL);
    } else if (ph_servizi_lavoro_annullato(j->l)) {
        sv_sessione_lascia(s);
        ph_servizi_lavoro_batteria(j->l, PH_SRV_ESITO_ERRORE, NULL, NULL);
    } else {
        PhDatiBatteria d;
        memset(&d, 0, sizeof d);
        PhErrore e = {{0}};
        int r = ph_dati_batteria(s->d, &d, &e);
        sv_sessione_lascia(s);
        if (r != 0) {
            fprintf(stderr, "ossatura: batteria del telefono: %s\n", e.testo);
            ph_servizi_lavoro_batteria(j->l, PH_SRV_ESITO_ERRORE, ph_t("Non riesco a leggere la batteria del telefono."),
                                       NULL);
        } else {
            PhSrvBatteria o = {
                .livello = d.livello > 100 ? 100 : d.livello,
                .stato = (PhSrvBatStato)d.stato, /* stesso ordine del protocollo */
                .alimentazione = d.alimentazione,
                .temperatura_nota = d.temperatura_nota,
                .temperatura_decimi = d.temperatura_nota ? d.temperatura_decimi : 0,
                .salute = d.salute <= PH_SRV_SALUTE_ERRORE ? (PhSrvSalute)d.salute : PH_SRV_SALUTE_SCONOSCIUTA,
                .tensione_mv = d.tensione_mv,
                .minuti_carica = d.minuti_carica,
                .minuti_autonomia = d.minuti_autonomia,
            };
            ph_servizi_lavoro_batteria(j->l, PH_SRV_ESITO_OK, NULL, &o);
        }
    }
    g_free(j);
}

void sv_tel_batteria(void *b, PhSrvLavoro *l)
{
    if (b == NULL) {
        ph_servizi_lavoro_batteria(l, PH_SRV_ESITO_ERRORE, NULL, NULL);
        return;
    }
    Sv *sv = b;
    sv_lavoro(sv, sv->p_lavori, batteria_lavoro, nuovo_job(l));
}

/* ---- Interruttori rapidi ---------------------------------------------------------------- */
static void ctl_converti(const PhDatiControlli *d, PhSrvControlli *o)
{
    memset(o, 0, sizeof *o);
    o->noto = true;
    o->suoneria = d->suoneria;
    o->non_disturbare = d->non_disturbare;
    o->torcia = d->torcia;
    o->trova_in_corso = d->trova_in_corso;
    o->puo_suoneria = d->puo_suoneria;
    o->puo_non_disturbare = d->puo_non_disturbare;
    o->puo_torcia = d->puo_torcia;
    o->puo_trova = d->puo_trova;
}

/* Telefono assente o illeggibile: tutto "non disponibile", mai un valore inventato. */
static void ctl_non_disponibili(PhSrvControlli *o)
{
    memset(o, 0, sizeof *o);
    o->suoneria = o->non_disturbare = o->torcia = PH_SRV_NON_DISPONIBILE;
}

/* ORDINE DELLE PUBBLICAZIONI. L'evento 0x8e (thread del componente) e la
 * risposta a una lettura (coda dello stato) arrivano da thread diversi, e il
 * telefono manda l'evento solo quando un controllo CAMBIA: se la lettura piu'
 * vecchia vincesse, il widget mostrerebbe uno stato sbagliato (torcia «spenta»
 * mentre e' accesa) fino al prossimo cambio. Percio' ctl_gen cresce a ogni
 * evento (e a ogni pubblicazione "non disponibile"), sempre sotto ctl_m; chi
 * legge annota ctl_gen PRIMA di chiedere e pubblica solo se non e' cambiato
 * (altrimenti un evento piu' nuovo ha gia' pubblicato). Pubblicare tenendo
 * ctl_m e' lecito: ph_servizi_pubblica_controlli non blocca e il thread GTK non
 * prende mai questo lucchetto. */
static void ctl_pubblica_nuovo(Sv *sv, const PhSrvControlli *c, bool trova)
{
    g_mutex_lock(&sv->ctl_m);
    sv->ctl_gen++;
    atomic_store(&sv->trova_attivo, trova);
    ph_servizi_pubblica_controlli(sv->serv, c);
    g_mutex_unlock(&sv->ctl_m);
}

/* Evento 0x8e (thread del componente: tornare subito, niente richieste). */
static void ev_controlli(const PhDatiControlli *d, void *utente)
{
    Sv *sv = utente;
    PhSrvControlli c;
    ctl_converti(d, &c);
    ctl_pubblica_nuovo(sv, &c, d->trova_in_corso);
}

/* Registra l'ascolto una volta sola per sessione (si "prenota" sotto s->m). */
static bool ctl_registra(Sv *sv, SvSessione *s)
{
    g_mutex_lock(&s->m);
    bool fare = !s->ascolto_controlli;
    s->ascolto_controlli = true;
    g_mutex_unlock(&s->m);
    if (!fare)
        return true;
    PhErrore e = {{0}};
    if (ph_dati_ascolta_controlli(s->d, ev_controlli, sv, &e) != 0) {
        g_mutex_lock(&s->m);
        s->ascolto_controlli = false;
        g_mutex_unlock(&s->m);
        return false;
    }
    return true;
}

static void ctl_ferma(SvSessione *s);

/* Parte l'osservazione: ascolto, poi lettura e pubblicazione dello stato. */
static void ctl_avvia(Sv *sv, SvSessione *s)
{
    PhSrvControlli c;
    PhDatiControlli d;
    PhErrore e = {{0}};
    bool registrato = ctl_registra(sv, s);
    /* Se nel frattempo l'ultimo osservatore se n'e' andato (osserva(false) gira
     * in parallelo) non si lascia un ascolto acceso senza nessuno che guardi. */
    g_mutex_lock(&sv->tm);
    bool ancora = sv->controlli_osserva;
    g_mutex_unlock(&sv->tm);
    if (!ancora) {
        ctl_ferma(s);
        return;
    }
    g_mutex_lock(&sv->ctl_m);
    guint64 g0 = sv->ctl_gen; /* PRIMA di chiedere */
    g_mutex_unlock(&sv->ctl_m);
    bool noto = registrato && ph_dati_controlli(s->d, &d, &e) == 0;
    if (noto)
        ctl_converti(&d, &c);
    else
        ctl_non_disponibili(&c);
    g_mutex_lock(&sv->ctl_m);
    if (sv->ctl_gen == g0) { /* nessun evento piu' nuovo e' passato nel frattempo */
        atomic_store(&sv->trova_attivo, noto ? d.trova_in_corso : false);
        ph_servizi_pubblica_controlli(sv->serv, &c);
    }
    g_mutex_unlock(&sv->ctl_m);
}

static void ctl_ferma(SvSessione *s)
{
    g_mutex_lock(&s->m);
    bool era = s->ascolto_controlli;
    s->ascolto_controlli = false;
    g_mutex_unlock(&s->m);
    if (era) {
        PhErrore e = {{0}};
        (void)ph_dati_ascolta_controlli(s->d, NULL, NULL, &e);
    }
}

static void osserva_lavoro(Sv *sv, void *dati)
{
    bool osserva = GPOINTER_TO_INT(dati) != 0;
    SvSessione *s = sv_sessione_usa(sv);
    if (s == NULL)
        return; /* alla prossima sessione si riparte se c'e' ancora un osservatore */
    if (osserva)
        ctl_avvia(sv, s);
    else
        ctl_ferma(s);
    sv_sessione_lascia(s);
}

/* Solo ai passaggi 0 -> 1 e 1 -> 0 del contatore degli osservatori. */
void sv_controlli_osserva(void *b, bool osserva)
{
    if (b == NULL)
        return;
    Sv *sv = b;
    g_mutex_lock(&sv->tm);
    sv->controlli_osserva = osserva;
    g_mutex_unlock(&sv->tm);
    sv_lavoro(sv, sv->p_stato, osserva_lavoro, GINT_TO_POINTER(osserva ? 1 : 0));
}

void sv_controlli_sessione_nuova(Sv *sv, SvSessione *s)
{
    g_mutex_lock(&sv->tm);
    bool osserva = sv->controlli_osserva;
    g_mutex_unlock(&sv->tm);
    if (osserva)
        ctl_avvia(sv, s); /* riconnessione: l'ascolto riparte e lo stato si ripubblica */
}

void sv_controlli_teardown(Sv *sv, SvSessione *s)
{
    bool chiusura = atomic_load(&sv->chiusura) != 0;
    /* Prima l'ascolto (aspetta un evento in corso), POI si legge e si azzera
     * trova_attivo: un evento in mezzo non lo rimette a vero nella sessione dopo. */
    ctl_ferma(s);
    bool trova = atomic_exchange(&sv->trova_attivo, false);
    if (trova && chiusura) {
        /* L'app si chiude con "trova il telefono" in corso: lo si ferma (volume
         * e suoneria li ripristina il telefono). */
        PhErrore e = {{0}};
        (void)ph_dati_trova_ferma(s->d, &e);
        ph_shell_decisione("trova il telefono fermato", "chiusura dell'applicazione");
    }
    g_mutex_lock(&sv->tm);
    bool osserva = sv->controlli_osserva;
    g_mutex_unlock(&sv->tm);
    if (osserva && !chiusura) { /* connessione persa: i controlli non sono piu' leggibili */
        PhSrvControlli c;
        ctl_non_disponibili(&c);
        ctl_pubblica_nuovo(sv, &c, false); /* ctl_gen cresce: una lettura in volo non lo copre */
    }
}

static void controllo_lavoro(Sv *sv, void *dati)
{
    NuovoJob *j = dati;
    SvSessione *s = sv_sessione_usa(sv);
    if (s == NULL) {
        ph_servizi_lavoro_fatto(j->l, PH_SRV_ESITO_NON_COLLEGATO, NULL);
    } else if (ph_servizi_lavoro_annullato(j->l)) {
        /* Chi ha chiesto ha rinunciato prima della partenza: nulla al telefono. */
        sv_sessione_lascia(s);
        ph_servizi_lavoro_fatto(j->l, PH_SRV_ESITO_ERRORE, NULL);
    } else {
        PhErrore e = {{0}};
        int r = -1;
        switch (j->ctl) {
        case PH_SRV_CTL_SUONERIA: r = ph_dati_suoneria_imposta(s->d, j->valore, &e); break;
        case PH_SRV_CTL_NON_DISTURBARE: r = ph_dati_non_disturbare_imposta(s->d, j->valore != 0, &e); break;
        case PH_SRV_CTL_TORCIA: r = ph_dati_torcia_imposta(s->d, j->valore != 0, &e); break;
        }
        sv_sessione_lascia(s);
        /* A schermo una frase nostra (tradotta); il dettaglio del telefono solo nel log. */
        if (r != 0)
            fprintf(stderr, "ossatura: controllo del telefono: %s\n", e.testo);
        ph_servizi_lavoro_fatto(j->l, r == 0 ? PH_SRV_ESITO_OK : PH_SRV_ESITO_ERRORE,
                                r == 0 ? NULL : ph_t("Il telefono non ha eseguito il comando."));
    }
    g_free(j);
}

void sv_controllo_imposta(void *b, PhSrvLavoro *l, PhSrvControllo controllo, int valore)
{
    if (b == NULL) {
        ph_servizi_lavoro_fatto(l, PH_SRV_ESITO_ERRORE, NULL);
        return;
    }
    Sv *sv = b;
    NuovoJob *j = nuovo_job(l);
    j->ctl = controllo;
    j->valore = valore;
    sv_lavoro(sv, sv->p_stato, controllo_lavoro, j);
}

static void trova_avvia_lavoro(Sv *sv, void *dati)
{
    NuovoJob *j = dati;
    SvSessione *s = sv_sessione_usa(sv);
    if (s == NULL) {
        ph_servizi_lavoro_fatto(j->l, PH_SRV_ESITO_NON_COLLEGATO, NULL);
    } else if (ph_servizi_lavoro_annullato(j->l)) {
        sv_sessione_lascia(s);
        ph_servizi_lavoro_fatto(j->l, PH_SRV_ESITO_ERRORE, NULL);
    } else {
        PhErrore e = {{0}};
        int sec = j->secondi < 5 ? 5 : (j->secondi > 120 ? 120 : j->secondi);
        int r = ph_dati_trova_avvia(s->d, (uint16_t)sec, &e);
        if (r == 0)
            atomic_store(&sv->trova_attivo, true); /* la chiusura dell'app lo fermera' */
        sv_sessione_lascia(s);
        if (r != 0)
            fprintf(stderr, "ossatura: trova il telefono: %s\n", e.testo);
        ph_servizi_lavoro_fatto(j->l, r == 0 ? PH_SRV_ESITO_OK : PH_SRV_ESITO_ERRORE,
                                r == 0 ? NULL : ph_t("Il telefono non ha eseguito il comando."));
    }
    g_free(j);
}

void sv_trova_avvia(void *b, PhSrvLavoro *l, int secondi)
{
    if (b == NULL) {
        ph_servizi_lavoro_fatto(l, PH_SRV_ESITO_ERRORE, NULL);
        return;
    }
    Sv *sv = b;
    NuovoJob *j = nuovo_job(l);
    j->secondi = secondi;
    sv_lavoro(sv, sv->p_stato, trova_avvia_lavoro, j);
}

static void trova_ferma_lavoro(Sv *sv, void *dati)
{
    (void)dati;
    SvSessione *s = sv_sessione_usa(sv);
    if (s == NULL)
        return;
    PhErrore e = {{0}};
    if (ph_dati_trova_ferma(s->d, &e) == 0)
        atomic_store(&sv->trova_attivo, false);
    sv_sessione_lascia(s);
}

void sv_trova_ferma(void *b)
{
    if (b == NULL)
        return;
    Sv *sv = b;
    sv_lavoro(sv, sv->p_stato, trova_ferma_lavoro, NULL);
}

#endif
