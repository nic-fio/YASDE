/* Chiamate recenti, rete e in riproduzione dei servizi veri (27 nov, PIANO.md 9).
 *
 * UN thread, creato alla prima osservazione e fermato in sv_sond_chiudi, legge dal
 * telefono (ph_dati_chiamate / _rete / _media) SOLO per le voci osservate, ai ritmi
 * del contratto (60 s, 15 s, 3 s), e pubblica con ph_servizi_pubblica_* quando il
 * valore e' cambiato. Mai sul thread GTK. Il lucchetto t->m protegge solo le
 * bandiere e i tempi: nessuna lettura dal telefono e nessuna pubblicazione lo
 * tengono. Ogni lettura "usa" la sessione (sv_sessione_usa / lascia), cosi' la
 * chiusura o la caduta del collegamento la aspettano e nessuna lettura tocca
 * memoria liberata. Senza sessione la voce e' «non disponibile» (si riprova ogni
 * secondo al massimo). I comandi al lettore musicale passano dallo stesso thread,
 * in ordine, e fanno rileggere subito il media.
 * Nomi, numeri, titoli e reti non vanno mai su stderr ne' in un log: nemmeno i
 * messaggi di errore di PhErrore (portano frasi del telefono) si stampano. */
#include <string.h>

#include "sv_interno.h"

#if PH_OSSATURA_SERVIZI_VERI

atomic_int sv_prova_sond_periodo_ms; /* solo prove: 0 = i ritmi del contratto */

static const int PERIODO_MS[SD_N] = {60000, 15000, 3000};
#define SOND_RITENTA_US (1000 * 1000) /* senza sessione: al massimo un controllo al secondo */

static gint64 periodo_us(int v)
{
    int prova = atomic_load(&sv_prova_sond_periodo_ms);
    return (gint64)(prova > 0 ? prova : PERIODO_MS[v]) * 1000;
}

static PhSrvLettura da_esito(uint8_t esito)
{
    return esito == 0 ? PH_SRV_LET_OK : esito == 2 ? PH_SRV_LET_NEGATO : PH_SRV_LET_NON_DISPONIBILE;
}

/* Ultimo valore pubblicato (solo il thread): per non rimandare ogni 3 s la stessa cosa. */
typedef struct {
    PhSrvChiamate chi;
    PhSrvReteDettaglio rete;
    PhSrvMedia media;
    bool ha[SD_N];
} Ultimi;

static void leggi_chiamate(Sv *sv, SvSessione *s, Ultimi *u)
{
    PhSrvChiamate c;
    memset(&c, 0, sizeof c);
    PhDatiChiamate d;
    PhErrore e = {{0}};
    memset(&d, 0, sizeof d);
    if (s == NULL || ph_dati_chiamate(s->d, &d, &e) != 0) {
        c.stato = PH_SRV_LET_NON_DISPONIBILE;
    } else {
        c.stato = da_esito(d.esito);
        if (c.stato == PH_SRV_LET_OK) {
            c.n = d.n < PH_SRV_CHIAMATE_MAX ? d.n : PH_SRV_CHIAMATE_MAX;
            for (size_t i = 0; i < c.n; i++) {
                c.v[i].tipo = d.v[i].tipo;
                c.v[i].ora_ms = d.v[i].ora_ms;
                c.v[i].durata_s = d.v[i].durata_s;
                g_strlcpy(c.v[i].nome, d.v[i].nome, sizeof c.v[i].nome);
                g_strlcpy(c.v[i].numero, d.v[i].numero, sizeof c.v[i].numero);
            }
        }
    }
    if (!u->ha[SD_CHI] || memcmp(&u->chi, &c, sizeof c) != 0) {
        u->chi = c;
        u->ha[SD_CHI] = true;
        ph_servizi_pubblica_chiamate(sv->serv, &c);
    }
}

static void leggi_rete(Sv *sv, SvSessione *s, Ultimi *u)
{
    PhSrvReteDettaglio r;
    memset(&r, 0, sizeof r);
    PhDatiRete d;
    PhErrore e = {{0}};
    memset(&d, 0, sizeof d);
    if (s == NULL || ph_dati_rete(s->d, &d, &e) != 0) {
        r.stato = PH_SRV_LET_NON_DISPONIBILE;
    } else {
        r.stato = da_esito(d.esito);
        if (r.stato == PH_SRV_LET_OK) {
            r.wifi = d.wifi;
            r.wifi_livello = d.wifi_livello;
            g_strlcpy(r.wifi_nome, d.wifi_nome, sizeof r.wifi_nome);
            r.mobile = d.mobile;
            r.mobile_livello = d.mobile_livello;
            r.generazione = d.generazione;
            g_strlcpy(r.operatore, d.operatore, sizeof r.operatore);
            r.dati_mobili = d.dati_mobili;
            r.aereo = d.aereo;
        }
    }
    if (!u->ha[SD_RETE] || memcmp(&u->rete, &r, sizeof r) != 0) {
        u->rete = r;
        u->ha[SD_RETE] = true;
        ph_servizi_pubblica_rete(sv->serv, &r);
    }
}

static void leggi_media(Sv *sv, SvSessione *s, Ultimi *u)
{
    PhSrvMedia m;
    memset(&m, 0, sizeof m);
    PhDatiMedia d;
    PhErrore e = {{0}};
    memset(&d, 0, sizeof d);
    if (s == NULL || ph_dati_media(s->d, &d, &e) != 0) {
        m.stato_lettura = PH_SRV_LET_NON_DISPONIBILE;
    } else {
        m.stato_lettura = da_esito(d.esito);
        if (m.stato_lettura == PH_SRV_LET_OK) {
            m.stato = d.stato;
            g_strlcpy(m.titolo, d.titolo, sizeof m.titolo);
            g_strlcpy(m.artista, d.artista, sizeof m.artista);
            g_strlcpy(m.pacchetto, d.pacchetto, sizeof m.pacchetto);
            m.comandi = d.comandi;
        }
    }
    if (!u->ha[SD_MEDIA] || memcmp(&u->media, &m, sizeof m) != 0) {
        u->media = m;
        u->ha[SD_MEDIA] = true;
        ph_servizi_pubblica_media(sv->serv, &m);
    }
}

static gpointer sond_corpo(gpointer p)
{
    Sv *sv = p;
    SvSond *t = &sv->sond;
    Ultimi *u = g_new0(Ultimi, 1);
    uint64_t ultima_sessione = 0;
    g_mutex_lock(&t->m);
    while (!atomic_load(&t->stop)) {
        gint64 ora = g_get_monotonic_time();
        gint64 sveglia = ora + SOND_RITENTA_US;
        bool leggi[SD_N] = {false};
        bool qualcuna = false;
        /* Sessione cambiata (caduta, ritorno, altro telefono): tutto si rilegge SUBITO e si
         * ripubblica, non alla scadenza: nulla del telefono di prima resta a schermo. */
        uint64_t sid = atomic_load(&sv->sess_id);
        if (sid != ultima_sessione) {
            ultima_sessione = sid;
            for (int i = 0; i < SD_N; i++)
                t->rifai[i] = true;
        }
        int cmd = 0;
        if (t->n_cmd > 0) {
            cmd = t->cmd[0];
            memmove(t->cmd, t->cmd + 1, (size_t)(t->n_cmd - 1) * sizeof t->cmd[0]);
            t->n_cmd--;
        }
        for (int i = 0; i < SD_N; i++) {
            if (!t->osserva[i]) {
                u->ha[i] = false;
                continue;
            }
            if (t->rifai[i]) {
                u->ha[i] = false; /* osservata da poco: il primo valore si pubblica sempre */
                t->rifai[i] = false;
                t->prossima_us[i] = 0;
            }
            if (t->prossima_us[i] <= ora) {
                leggi[i] = true;
                qualcuna = true;
            } else if (t->prossima_us[i] < sveglia) {
                sveglia = t->prossima_us[i];
            }
        }
        if (cmd != 0 && t->osserva[SD_MEDIA]) {
            leggi[SD_MEDIA] = true; /* dopo il comando si rilegge subito */
            qualcuna = true;
        }
        if (!qualcuna && cmd == 0) {
            g_cond_wait_until(&t->cv, &t->m, sveglia);
            continue;
        }
        g_mutex_unlock(&t->m);

        SvSessione *s = sv_sessione_usa(sv);
        /* Alla chiusura non si aspettano le letture che restano: stop prima di ognuna. */
        if (cmd != 0 && s != NULL && !atomic_load(&t->stop)) {
            PhErrore e = {{0}};
            (void)ph_dati_media_comando(s->d, (uint8_t)cmd, &e); /* l'esito si vede dalla rilettura */
        }
        if (leggi[SD_CHI] && !atomic_load(&t->stop))
            leggi_chiamate(sv, s, u);
        if (leggi[SD_RETE] && !atomic_load(&t->stop))
            leggi_rete(sv, s, u);
        if (leggi[SD_MEDIA] && !atomic_load(&t->stop))
            leggi_media(sv, s, u);
        gint64 fine = g_get_monotonic_time();
        if (s != NULL)
            sv_sessione_lascia(s);

        g_mutex_lock(&t->m);
        for (int i = 0; i < SD_N; i++) {
            if (!leggi[i])
                continue;
            gint64 p_us = periodo_us(i);
            if (s == NULL && p_us > SOND_RITENTA_US)
                p_us = SOND_RITENTA_US;
            t->prossima_us[i] = fine + p_us;
        }
    }
    g_mutex_unlock(&t->m);
    g_free(u);
    return NULL;
}

static void osserva_voce(Sv *sv, int v, bool osserva)
{
    SvSond *t = &sv->sond;
    g_mutex_lock(&t->m);
    if (atomic_load(&t->stop)) {
        g_mutex_unlock(&t->m);
        return;
    }
    t->osserva[v] = osserva;
    if (osserva) {
        t->rifai[v] = true;
        if (t->th == NULL)
            t->th = g_thread_new("ph-sondaggi", sond_corpo, sv);
    }
    g_cond_broadcast(&t->cv);
    g_mutex_unlock(&t->m);
}

void sv_sond_inizia(Sv *sv)
{
    g_mutex_init(&sv->sond.m);
    g_cond_init(&sv->sond.cv);
}

void sv_chiamate_osserva(void *b, bool osserva) { osserva_voce(b, SD_CHI, osserva); }
void sv_rete_osserva(void *b, bool osserva) { osserva_voce(b, SD_RETE, osserva); }
void sv_media_osserva(void *b, bool osserva) { osserva_voce(b, SD_MEDIA, osserva); }

void sv_media_comando(void *b, int comando)
{
    Sv *sv = b;
    SvSond *t = &sv->sond;
    g_mutex_lock(&t->m);
    if (!atomic_load(&t->stop) && t->th != NULL && t->n_cmd < SD_CMD_MAX) {
        t->cmd[t->n_cmd++] = comando;
        g_cond_broadcast(&t->cv);
    }
    g_mutex_unlock(&t->m);
}

void sv_sond_chiudi(Sv *sv)
{
    SvSond *t = &sv->sond;
    g_mutex_lock(&t->m);
    atomic_store(&t->stop, true);
    GThread *th = t->th;
    t->th = NULL;
    g_cond_broadcast(&t->cv);
    g_mutex_unlock(&t->m);
    if (th != NULL)
        g_thread_join(th); /* una lettura in corso ha la sua scadenza (ph_dati_*) */
}

void sv_sond_sveglia(Sv *sv)
{
    g_mutex_lock(&sv->sond.m);
    g_cond_broadcast(&sv->sond.cv);
    g_mutex_unlock(&sv->sond.m);
}

void sv_sond_libera(Sv *sv)
{
    g_mutex_clear(&sv->sond.m);
    g_cond_clear(&sv->sond.cv);
}

#endif
