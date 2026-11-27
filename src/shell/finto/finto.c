/* Backend finto dei servizi (finto.h): stato, thread di lavoro, app,
 * notifiche, albero di cartelle, trasferimenti. I flussi sono in
 * flusso_finto.c, i comandi e il demo in comandi.c. */
#include <cairo.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "interno.h"

/* ---- Dati del mockup ------------------------------------------------------- */
typedef struct {
    const char *pacchetto, *nome, *mono;
    uint32_t colore;
    bool verticale, sistema;
} AppFinta;

/* APPS del mockup (stessi nomi, iniziali e colori) + "Banca" per lo schermo
 * protetto. Le solo-verticali sono quelle con rs:false in SPEC. */
static const AppFinta APP[] = {
    {"it.finto.messaggi", "Messages", "Ms", 0x2563eb, true, false},
    {"it.finto.browser", "Browser", "Br", 0xb45309, false, false},
    {"it.finto.note", "Notes", "No", 0x047857, false, false},
    {"it.finto.foto", "Photos", "Ph", 0xbe185d, false, false},
    {"it.finto.calcolatrice", "Calculator", "Ca", 0x7c3aed, true, false},
    {"it.finto.mappe", "Maps", "Ma", 0x0f766e, false, false},
    {"it.finto.musica", "Music", "Mu", 0xb91c1c, true, false},
    {"it.finto.impostazioni", "Settings", "Se", 0x475569, false, true},
    {"it.finto.fotocamera", "Camera", "Cm", 0x0369a1, false, false},
    {"it.finto.calendario", "Calendar", "Cl", 0x4338ca, false, false},
    {"it.finto.orologio", "Clock", "Ck", 0x0e7490, false, true},
    {"it.finto.contatti", "Contacts", "Ct", 0x15803d, false, false},
    {"it.finto.file", "File", "Fi", 0xa16207, false, false},
    {"it.finto.meteo", "Weather", "We", 0x1d4ed8, false, false},
    {"it.finto.video", "Video", "Vi", 0x9d174d, false, false},
    {"it.finto.chiamate", "Phone", "Pn", 0x166534, false, true},
    {"it.finto.banca", "Bank", "Bk", 0x334155, false, false},
};

uint32_t finto_colore_app(const char *pacchetto)
{
    for (size_t i = 0; pacchetto != NULL && i < G_N_ELEMENTS(APP); i++)
        if (strcmp(APP[i].pacchetto, pacchetto) == 0)
            return APP[i].colore;
    return 0x475569;
}

typedef struct {
    const char *pacchetto, *titolo, *testo;
} NotificaFinta;

static const NotificaFinta NOTIFICHE_INIZIALI[] = {
    {"it.finto.messaggi", "New message", "Are we still on for 6 pm?"},
    {"it.finto.browser", "Download complete", "report.pdf"},
    {"it.finto.impostazioni", "Update available", "A new system version"},
};

static const NotificaFinta NOTIFICHE_NUOVE[] = {
    {"it.finto.messaggi", "Meeting moved to 3 pm", "Updated by the organizer"},
    {"it.finto.browser", "Download complete", "invoice.pdf"},
    {"it.finto.impostazioni", "Battery almost full", "The phone reached 80%"},
};

/* ---- Thread di lavoro ------------------------------------------------------ */
typedef struct {
    PhFinto *f;
    void (*fn)(PhFinto *f, void *dati);
    void *dati;
    GDestroyNotify libera;
} Lavoro;

static gboolean lavoro_esegui(gpointer p)
{
    Lavoro *l = p;
    l->fn(l->f, l->dati);
    return G_SOURCE_REMOVE;
}

static void lavoro_libera(gpointer p)
{
    Lavoro *l = p;
    if (l->libera != NULL)
        l->libera(l->dati);
    g_free(l);
}

void finto_lavora(PhFinto *f, int ritardo_ms, void (*fn)(PhFinto *f, void *dati), void *dati,
                  GDestroyNotify libera)
{
    Lavoro *l = g_new(Lavoro, 1);
    l->f = f;
    l->fn = fn;
    l->dati = dati;
    l->libera = libera;
    GSource *s = ritardo_ms > 0 ? g_timeout_source_new((guint)ritardo_ms) : g_idle_source_new();
    /* Stessa priorita' per comandi e richieste: a parita' GLib serve le sorgenti
     * pronte nell'ordine in cui sono state aggiunte, quindi un comando dato prima
     * di una richiesta vale per quella richiesta anche se il thread e' rimasto
     * fermo oltre il ritardo (l'idle di norma ha priorita' piu' bassa). */
    g_source_set_priority(s, G_PRIORITY_DEFAULT);
    g_source_set_callback(s, lavoro_esegui, l, lavoro_libera);
    g_source_attach(s, f->ctx);
    g_source_unref(s);
}

static gpointer thread_lavoro(gpointer p)
{
    PhFinto *f = p;
    g_main_context_push_thread_default(f->ctx);
    g_main_loop_run(f->loop);
    g_main_context_pop_thread_default(f->ctx);
    return NULL;
}

static int ritardo(PhFinto *f)
{
    return f->o.ritardo_ms > 0 ? f->o.ritardo_ms : 150;
}

/* ---- Pubblicazioni (thread di lavoro) ------------------------------------- */
static void pubblica_collegamento(PhFinto *f)
{
    PhSrvCollegamento c = {0};
    c.stato = f->stato;
    c.ha_telefono = f->stato == PH_SRV_COLLEGATO || f->stato == PH_SRV_BLOCCATO;
    g_strlcpy(c.nome, "Demo phone", sizeof c.nome);
    c.api = 34;
    c.schermo_w = 1080;
    c.schermo_h = 2340;
    c.densita = 420;
    const char *m = "";
    switch (f->stato) {
    case PH_SRV_CERCO: m = "Looking for the demo phone"; break;
    case PH_SRV_COLLEGATO: m = "Connected to the demo phone"; break;
    case PH_SRV_BLOCCATO: m = "The demo phone is locked"; break;
    case PH_SRV_PERSO: m = "Connection lost (simulated): retrying"; break;
    case PH_SRV_CHIUSO: m = "Closed"; break;
    case PH_SRV_NON_ABBINATO: m = "The phone does not recognize this PC: pair it with the code"; break;
    }
    g_strlcpy(c.motivo, m, sizeof c.motivo);
    c.schermo_spento = c.ha_telefono && f->schermo_spento;
    c.telefoni_noti = f->noti;
    c.telefoni_trovati = f->trovati;
    ph_servizi_pubblica_collegamento(f->s, &c);
}

static void pubblica_telefono(PhFinto *f)
{
    PhSrvTelefono t = {0};
    bool c = f->stato == PH_SRV_COLLEGATO || f->stato == PH_SRV_BLOCCATO;
    t.noto = c;
    t.batteria = f->batteria;
    t.in_carica = f->in_carica;
    t.rete = PH_SRV_RETE_WIFI;
    t.segnale = 3;
    t.pannello_noto = c;
    t.pannello_acceso = f->pannello;
    t.in_mano = f->in_mano;
    ph_servizi_pubblica_telefono(f->s, &t);
}

static void pubblica_app(PhFinto *f)
{
    size_t n = G_N_ELEMENTS(APP);
    PhSrvApp *v = calloc(n, sizeof *v);
    if (v == NULL)
        return;
    size_t k = 0;
    for (size_t i = 0; i < n; i++) {
        if (g_hash_table_contains(f->disinstallate, APP[i].pacchetto))
            continue;
        v[k].pacchetto = strdup(APP[i].pacchetto);
        v[k].nome = strdup(APP[i].nome);
        v[k].di_sistema = APP[i].sistema;
        v[k].solo_verticale = APP[i].verticale;
        k++;
    }
    ph_servizi_pubblica_app(f->s, v, k);
}

/* prima: e' la prima lettura dopo un collegamento (servizi.h, (6)). */
static void pubblica_notifiche_lettura(PhFinto *f, bool prima)
{
    if (f->notifiche_illeggibili) {
        ph_servizi_pubblica_notifiche_lettura(f->s, NULL, 0,
                                              "Il telefono non permette di leggere le notifiche (simulato)",
                                              prima);
        return;
    }
    size_t n = f->notifiche->len;
    PhSrvNotifica *v = n > 0 ? calloc(n, sizeof *v) : NULL;
    for (size_t i = 0; v != NULL && i < n; i++) {
        const PhSrvNotifica *o = g_ptr_array_index(f->notifiche, i);
        v[i].chiave = strdup(o->chiave);
        v[i].pacchetto = strdup(o->pacchetto);
        v[i].titolo = strdup(o->titolo);
        v[i].testo = strdup(o->testo);
        v[i].ora_ms = o->ora_ms;
    }
    ph_servizi_pubblica_notifiche_lettura(f->s, v, v != NULL ? n : 0, NULL, prima);
}

static void pubblica_notifiche(PhFinto *f)
{
    pubblica_notifiche_lettura(f, false);
}

static bool collegato(PhFinto *f);
static void meteo_cambiato(PhFinto *f);
static void meteo_riparti(PhFinto *f);

/* Fotografia dei controlli: solo mentre qualcuno osserva (servizi_backend.h) e
 * a telefono collegato. */
static void pubblica_controlli(PhFinto *f)
{
    if (!f->ctl_osservati || !collegato(f))
        return;
    PhSrvControlli c = {0};
    bool manca = f->modo[PH_FINTO_VOCE_CONTROLLI] == PH_FINTO_MODO_NON_DISPONIBILE;
    c.noto = true;
    c.suoneria = manca ? PH_SRV_NON_DISPONIBILE : f->suoneria;
    c.non_disturbare = manca ? PH_SRV_NON_DISPONIBILE : f->non_disturbare;
    c.torcia = manca || f->torcia_manca ? PH_SRV_NON_DISPONIBILE : f->torcia;
    c.trova_in_corso = f->trova_attivo;
    c.puo_suoneria = c.puo_non_disturbare = c.puo_trova = !manca;
    c.puo_torcia = !manca && !f->torcia_manca;
    ph_servizi_pubblica_controlli(f->s, &c);
}

static void notifiche_dopo(PhFinto *f, void *dati)
{
    (void)dati;
    if (collegato(f))
        pubblica_notifiche(f);
}

static void pubblica_tr(PhFinto *f)
{
    size_t n = f->tr->len;
    PhSrvTrasferimento *v = n > 0 ? calloc(n, sizeof *v) : NULL;
    for (size_t i = 0; v != NULL && i < n; i++) {
        const PhSrvTrasferimento *o = g_ptr_array_index(f->tr, i);
        v[i] = *o;
        v[i].nome = strdup(o->nome);
        v[i].origine = strdup(o->origine);
        v[i].destinazione = strdup(o->destinazione);
        v[i].errore = strdup(o->errore);
    }
    ph_servizi_pubblica_trasferimenti(f->s, v, v != NULL ? n : 0, f->stato != PH_SRV_COLLEGATO &&
                                                                       f->stato != PH_SRV_BLOCCATO);
}

static void pubblica_volume(PhFinto *f)
{
    PhSrvVolume v = {f->volume, f->muto};
    ph_servizi_pubblica_volume(f->s, &v);
}

/* ---- Collegamento ---------------------------------------------------------- */
static void collegati(PhFinto *f, void *dati)
{
    (void)dati;
    f->ritorno = 0;
    bool primo = f->stato == PH_SRV_CERCO && f->profilo_salvato == NULL;
    if (f->noti == 0)
        f->noti = 1;
    f->trovati = 1;
    f->stato = f->bloccato ? PH_SRV_BLOCCATO : PH_SRV_COLLEGATO;
    fprintf(stderr, "finto: collegato\n");
    pubblica_collegamento(f);
    pubblica_telefono(f);
    pubblica_app(f);
    if (f->prima_vuota) {
        /* Prima lettura vuota (il telefono non ha ancora la lista), poi la lista
         * vera dopo 1 s: quelle notifiche risultano «nuove». */
        ph_servizi_pubblica_notifiche_lettura(f->s, NULL, 0, NULL, true);
        finto_lavora(f, 1000, notifiche_dopo, NULL, NULL);
    } else {
        pubblica_notifiche_lettura(f, true);
    }
    pubblica_volume(f);
    pubblica_tr(f);
    pubblica_controlli(f);
    meteo_riparti(f);
    finto_flussi_perso(f, false);
    if (primo) {
        /* Profilo del telefono finto: i predefiniti di config piu' le 4 icone
         * del mockup (deskPos: msg, web, note, photo nella colonna 0). */
        PhCfgTelefono *p = ph_cfg_telefono_nuovo();
        if (p != NULL) {
            static const char *const icone[] = {"it.finto.messaggi", "it.finto.browser", "it.finto.note",
                                                "it.finto.foto"};
            for (size_t i = 0; i < G_N_ELEMENTS(icone); i++) {
                g_strlcpy(p->icone[i].app, icone[i], sizeof p->icone[i].app);
                p->icone[i].colonna = 0;
                p->icone[i].riga = (int)i;
            }
            p->n_icone = G_N_ELEMENTS(icone);
            /* Due app fissate sulla barra (richieste del committente, PIANO.md):
             * una che si apre spesso e una non aperta. */
            if (ph_cfg_telefono_fissa_app(p, "it.finto.messaggi", NULL) != 0 ||
                ph_cfg_telefono_fissa_app(p, "it.finto.calcolatrice", NULL) != 0)
                fprintf(stderr, "finto: app fissate non impostate\n");
            p->volume = 100;
            f->volume = 100;
            pubblica_volume(f);
            ph_servizi_pubblica_profilo(f->s, p);
        }
    }
}

static void non_abbinato(PhFinto *f, void *dati)
{
    (void)dati;
    f->stato = PH_SRV_NON_ABBINATO;
    f->trovati = 1;
    fprintf(stderr, "finto: telefono non abbinato\n");
    pubblica_collegamento(f);
}

/* In CERCO: se il telefono e' in rete, il noto si collega e il nuovo chiede
 * il codice; altrimenti si resta in CERCO. */
static void cerca(PhFinto *f)
{
    if (!f->raggiungibile) {
        f->trovati = 0;
        pubblica_collegamento(f);
        return;
    }
    if (f->noti > 0)
        finto_lavora(f, 300, collegati, NULL, NULL);
    else
        finto_lavora(f, 300, non_abbinato, NULL, NULL);
}

static void avvia(PhFinto *f, void *dati)
{
    (void)dati;
    f->stato = PH_SRV_CERCO;
    switch ((PhFintoInizio)f->o.inizio) {
    case PH_FINTO_INIZIO_NON_ABBINATO:
        f->noti = 0;
        f->raggiungibile = true;
        break;
    case PH_FINTO_INIZIO_CERCA_NOTO:
        f->noti = 1;
        f->raggiungibile = false;
        break;
    case PH_FINTO_INIZIO_CERCA_NUOVO:
        f->noti = 0;
        f->raggiungibile = false;
        break;
    case PH_FINTO_INIZIO_COLLEGATO:
    default:
        f->noti = 1;
        f->raggiungibile = true;
        break;
    }
    pubblica_collegamento(f);
    cerca(f);
}

static void fai_blocco(PhFinto *f, void *dati)
{
    bool b = GPOINTER_TO_INT(dati) != 0;
    f->bloccato = b;
    if (f->stato == PH_SRV_COLLEGATO || f->stato == PH_SRV_BLOCCATO) {
        f->stato = b ? PH_SRV_BLOCCATO : PH_SRV_COLLEGATO;
        fprintf(stderr, "finto: telefono %s\n", b ? "bloccato" : "sbloccato");
        pubblica_collegamento(f);
        pubblica_telefono(f);
    }
}

static gboolean ritorna(gpointer p)
{
    collegati(p, NULL);
    return G_SOURCE_REMOVE;
}

static void fai_perdita(PhFinto *f, void *dati)
{
    int secondi = GPOINTER_TO_INT(dati);
    if (f->stato != PH_SRV_COLLEGATO && f->stato != PH_SRV_BLOCCATO)
        return;
    f->stato = PH_SRV_PERSO;
    meteo_cambiato(f); /* niente telefono: nessuna posizione */
    fprintf(stderr, "finto: connessione persa per %d s\n", secondi);
    /* La voce in corso fallisce e la coda si ferma (come trasferimenti.h). */
    for (guint i = 0; i < f->tr->len; i++) {
        PhSrvTrasferimento *t = g_ptr_array_index(f->tr, i);
        if (t->stato == PH_SRV_TR_CORSO) {
            t->stato = PH_SRV_TR_FALLITO;
            free(t->errore);
            t->errore = strdup("Connessione persa");
        }
    }
    pubblica_collegamento(f);
    pubblica_telefono(f);
    pubblica_tr(f);
    finto_flussi_perso(f, true);
    GSource *s = g_timeout_source_new((guint)(secondi > 0 ? secondi : 5) * 1000u);
    g_source_set_callback(s, ritorna, f, NULL);
    f->ritorno = g_source_attach(s, f->ctx);
    g_source_unref(s);
}

static void fai_riconnetti(PhFinto *f, void *dati)
{
    (void)dati;
    if (f->stato == PH_SRV_CERCO) {
        fprintf(stderr, "finto: cerco di nuovo\n");
        cerca(f);
        return;
    }
    if (f->stato == PH_SRV_NON_ABBINATO) {
        /* Riprova senza abbinare: la chiave resta sconosciuta. */
        fprintf(stderr, "finto: riprovo senza abbinare\n");
        pubblica_collegamento(f);
        return;
    }
    if (f->stato != PH_SRV_PERSO)
        return;
    if (f->ritorno != 0) {
        GSource *s = g_main_context_find_source_by_id(f->ctx, f->ritorno);
        if (s != NULL)
            g_source_destroy(s);
        f->ritorno = 0;
    }
    fprintf(stderr, "finto: riconnetti ora\n");
    collegati(f, NULL);
}

/* ---- Stato che cambia col tempo -------------------------------------------- */
static void aggiungi_notifica(PhFinto *f, const NotificaFinta *n)
{
    PhSrvNotifica *v = calloc(1, sizeof *v);
    if (v == NULL)
        return;
    static int contatore;
    char chiave[32];
    snprintf(chiave, sizeof chiave, "finta-%d", ++contatore);
    v->chiave = strdup(chiave);
    v->pacchetto = strdup(n->pacchetto);
    v->titolo = strdup(n->titolo);
    v->testo = strdup(n->testo);
    v->ora_ms = (uint64_t)(g_get_real_time() / 1000);
    g_ptr_array_insert(f->notifiche, 0, v);
}

static void notifica_libera(gpointer p)
{
    PhSrvNotifica *v = p;
    ph_srv_notifiche_libera(v, 1);
}

static void fai_notifica(PhFinto *f, void *dati)
{
    (void)dati;
    const NotificaFinta *n = &NOTIFICHE_NUOVE[f->prossima_notifica++ % G_N_ELEMENTS(NOTIFICHE_NUOVE)];
    aggiungi_notifica(f, n);
    fprintf(stderr, "finto: notifica nuova \"%s\"\n", n->titolo);
    if (f->stato == PH_SRV_COLLEGATO || f->stato == PH_SRV_BLOCCATO)
        pubblica_notifiche(f);
}

static void fai_aggiorna_notifica(PhFinto *f, void *dati)
{
    (void)dati;
    if (f->notifiche->len == 0)
        return;
    PhSrvNotifica *v = g_ptr_array_index(f->notifiche, 0);
    char *t = g_strdup_printf("%s (aggiornata)", v->testo);
    free(v->testo);
    v->testo = strdup(t);
    g_free(t);
    v->ora_ms = (uint64_t)(g_get_real_time() / 1000);
    fprintf(stderr, "finto: notifica aggiornata\n");
    pubblica_notifiche(f);
}

static void fai_illeggibili(PhFinto *f, void *dati)
{
    f->notifiche_illeggibili = GPOINTER_TO_INT(dati) != 0;
    pubblica_notifiche(f);
}

static void fai_batteria(PhFinto *f, void *dati)
{
    f->batteria = CLAMP(GPOINTER_TO_INT(dati), 0, 100);
    pubblica_telefono(f);
}

static gboolean ogni_minuto(gpointer p)
{
    PhFinto *f = p;
    if (f->in_carica) {
        if (f->batteria < 100)
            f->batteria++;
    } else if (f->batteria > 5) {
        f->batteria--;
    }
    if (f->stato == PH_SRV_COLLEGATO || f->stato == PH_SRV_BLOCCATO)
        pubblica_telefono(f);
    return G_SOURCE_CONTINUE;
}

static gboolean ogni_notifica(gpointer p)
{
    fai_notifica(p, NULL);
    return G_SOURCE_CONTINUE;
}

/* ---- Albero di cartelle del mockup ----------------------------------------- */
typedef struct {
    char *nome;
    bool cartella;
    uint64_t dimensione;
    int64_t modificato;
} Voce;

static void voce_libera(gpointer p)
{
    Voce *v = p;
    free(v->nome);
    g_free(v);
}

static GPtrArray *cartella_di(PhFinto *f, const char *percorso, bool crea)
{
    GPtrArray *a = g_hash_table_lookup(f->albero, percorso);
    if (a == NULL && crea) {
        a = g_ptr_array_new_with_free_func(voce_libera);
        g_hash_table_insert(f->albero, g_strdup(percorso), a);
    }
    return a;
}

static char *unisci(const char *cartella, const char *nome)
{
    return strcmp(cartella, "/") == 0 ? g_strconcat("/", nome, NULL) : g_strconcat(cartella, "/", nome, NULL);
}

static void aggiungi_voce(PhFinto *f, const char *cartella, const char *nome, bool dir, uint64_t dim)
{
    GPtrArray *a = cartella_di(f, cartella, true);
    for (guint i = 0; i < a->len; i++)
        if (strcmp(((Voce *)g_ptr_array_index(a, i))->nome, nome) == 0)
            return;
    Voce *v = g_new0(Voce, 1);
    v->nome = strdup(nome);
    v->cartella = dir;
    v->dimensione = dim;
    v->modificato = g_get_real_time() / 1000000 - (int64_t)(g_str_hash(nome) % (30u * 86400u));
    g_ptr_array_add(a, v);
    if (dir) {
        char *p = unisci(cartella, nome);
        cartella_di(f, p, true);
        g_free(p);
    }
}

static bool non_consentita(const char *percorso)
{
    return strcmp(percorso, "/mnt") == 0 || strcmp(percorso, "/data") == 0 ||
           strcmp(percorso, "/system") == 0 || g_str_has_prefix(percorso, "/data/") ||
           g_str_has_prefix(percorso, "/system/");
}

static void costruisci_albero(PhFinto *f)
{
    f->albero = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, (GDestroyNotify)g_ptr_array_unref);
    const char *B = "/storage/emulated/0";
    aggiungi_voce(f, "/", "storage", true, 0);
    aggiungi_voce(f, "/", "mnt", true, 0);
    aggiungi_voce(f, "/", "data", true, 0);
    aggiungi_voce(f, "/", "system", true, 0);
    aggiungi_voce(f, "/storage", "emulated", true, 0);
    aggiungi_voce(f, "/storage/emulated", "0", true, 0);
    static const char *const dirs[] = {"Download", "DCIM", "Documents", "Pictures", "Music", "Chat", "projects"};
    for (size_t i = 0; i < G_N_ELEMENTS(dirs); i++)
        aggiungi_voce(f, B, dirs[i], true, 0);
    static const struct {
        const char *dir, *nome;
        bool cartella;
        uint64_t dim;
    } voci[] = {
        {"/projects", "old", true, 0},
        {"/projects", "floor_plan.pdf", false, 760000},
        {"/projects", "voice_memo.m4a", false, 1900000},
        {"/projects/vecchi", "draft_1.txt", false, 2048},
        {"/Chat", "Media", true, 0},
        {"/Chat/Media", "Chat Documents", true, 0},
        {"/Chat/Media/Chat Documents", "quote.pdf", false, 412000},
        {"/Chat/Media/Chat Documents", "blueprint.pdf", false, 1850000},
        {"/Download", "report.pdf", false, 2516582},
        {"/Download", "invoice_october.pdf", false, 318000},
        {"/Download", "setup.apk", false, 50331648},
        {"/Download", "notes.txt", false, 4096},
        {"/Download", ".hidden", false, 10},
        {"/DCIM", "Camera", true, 0},
        {"/DCIM/Camera", "IMG_0412.jpg", false, 4300000},
        {"/DCIM/Camera", "IMG_0398.jpg", false, 3900000},
        {"/DCIM/Camera", "IMG_0377.jpg", false, 4100000},
        {"/DCIM/Camera", "VID_0120.mp4", false, 90177536},
        {"/Documents", "contract.docx", false, 122880},
        {"/Documents", "budget.xlsx", false, 87040},
        {"/Pictures", "Screenshots", true, 0},
        {"/Pictures/Screenshots", "Screenshot_1120.png", false, 1258291},
        {"/Music", "track_01.mp3", false, 5872025},
        {"/Music", "track_02.mp3", false, 7444889},
    };
    for (size_t i = 0; i < G_N_ELEMENTS(voci); i++) {
        char *d = g_strconcat(B, voci[i].dir, NULL);
        aggiungi_voce(f, d, voci[i].nome, voci[i].cartella, voci[i].dim);
        g_free(d);
    }
}

static int confronta_voci(gconstpointer a, gconstpointer b)
{
    const Voce *x = *(Voce *const *)a, *y = *(Voce *const *)b;
    if (x->cartella != y->cartella)
        return x->cartella ? -1 : 1;
    char *cx = g_utf8_casefold(x->nome, -1), *cy = g_utf8_casefold(y->nome, -1);
    int r = strcmp(cx, cy);
    g_free(cx);
    g_free(cy);
    return r;
}

/* "/sdcard" vale la memoria interna; niente "/" finale. */
static char *normalizza(const char *p)
{
    char *s = g_strdup(p);
    if (g_str_has_prefix(s, "/sdcard") && (s[7] == '\0' || s[7] == '/')) {
        char *t = g_strconcat("/storage/emulated/0", s + 7, NULL);
        g_free(s);
        s = t;
    }
    size_t n = strlen(s);
    while (n > 1 && s[n - 1] == '/')
        s[--n] = '\0';
    return s;
}

/* ---- Lavori ---------------------------------------------------------------- */
typedef struct {
    PhSrvLavoro *l;
    bool finito;
    char *a, *b;
    bool flag;
    unsigned numero;
    int lato;
    size_t massimo;
} Richiesta;

static Richiesta *richiesta(PhSrvLavoro *l, const char *a, const char *b)
{
    Richiesta *r = g_new0(Richiesta, 1);
    r->l = l;
    r->a = a != NULL ? g_strdup(a) : NULL;
    r->b = b != NULL ? g_strdup(b) : NULL;
    return r;
}

/* Se il thread si ferma prima, il lavoro si chiude comunque (servizi_backend.h). */
static void richiesta_libera(gpointer p)
{
    Richiesta *r = p;
    if (!r->finito)
        ph_servizi_lavoro_fatto(r->l, PH_SRV_ESITO_ERRORE, "chiuso");
    g_free(r->a);
    g_free(r->b);
    g_free(r);
}

static bool collegato(PhFinto *f)
{
    return f->stato == PH_SRV_COLLEGATO || f->stato == PH_SRV_BLOCCATO;
}

static void fai_elenca(PhFinto *f, void *dati)
{
    Richiesta *r = dati;
    r->finito = true;
    char *p = normalizza(r->a);
    if (!collegato(f)) {
        ph_servizi_lavoro_elenco(r->l, PH_SRV_ESITO_NON_COLLEGATO, "Telefono non collegato", NULL, 0);
    } else if (non_consentita(p)) {
        ph_servizi_lavoro_elenco(r->l, PH_SRV_ESITO_NON_CONSENTITO, NULL, NULL, 0);
    } else {
        GPtrArray *a = cartella_di(f, p, false);
        if (a == NULL) {
            ph_servizi_lavoro_elenco(r->l, PH_SRV_ESITO_NON_TROVATO, NULL, NULL, 0);
        } else {
            g_ptr_array_sort(a, confronta_voci);
            PhSrvVoce *v = a->len > 0 ? calloc(a->len, sizeof *v) : NULL;
            for (guint i = 0; v != NULL && i < a->len; i++) {
                const Voce *o = g_ptr_array_index(a, i);
                v[i].nome = strdup(o->nome);
                v[i].nome_schermo = strdup(o->nome);
                v[i].cartella = o->cartella;
                v[i].dimensione = o->dimensione;
                v[i].modificato = o->modificato;
            }
            ph_servizi_lavoro_elenco(r->l, PH_SRV_ESITO_OK, NULL, v, v != NULL ? a->len : 0);
        }
    }
    g_free(p);
}

static void fai_cerca(PhFinto *f, void *dati)
{
    Richiesta *r = dati;
    r->finito = true;
    if (!collegato(f)) {
        ph_servizi_lavoro_cerca_fine(r->l, PH_SRV_ESITO_NON_COLLEGATO, "Telefono non collegato", false);
        return;
    }
    char *base = normalizza(r->a);
    char *ago = g_utf8_casefold(r->b, -1);
    unsigned trovati = 0;
    bool massimo = false;
    GHashTableIter it;
    gpointer k, val;
    GArray *gruppo = g_array_new(FALSE, TRUE, sizeof(PhSrvTrovato));
    GArray *dimensioni = g_array_new(FALSE, TRUE, sizeof(uint64_t));
    GPtrArray *tenere = g_ptr_array_new_with_free_func(g_free);
    g_hash_table_iter_init(&it, f->albero);
    while (g_hash_table_iter_next(&it, &k, &val) && !massimo) {
        const char *dir = k;
        if (!(strcmp(base, "/") == 0 || strcmp(dir, base) == 0 ||
              (g_str_has_prefix(dir, base) && dir[strlen(base)] == '/')) ||
            non_consentita(dir))
            continue;
        GPtrArray *a = val;
        for (guint i = 0; i < a->len; i++) {
            const Voce *v = g_ptr_array_index(a, i);
            if (r->flag && !v->cartella)
                continue;
            char *n = g_utf8_casefold(v->nome, -1);
            bool si = strstr(n, ago) != NULL;
            g_free(n);
            if (!si)
                continue;
            if (trovati == r->numero) {
                massimo = true;
                break;
            }
            char *pieno = unisci(dir, v->nome);
            g_ptr_array_add(tenere, pieno);
            PhSrvTrovato t = {pieno, pieno, v->cartella};
            uint64_t dim = v->cartella ? UINT64_MAX : v->dimensione;
            g_array_append_val(dimensioni, dim);
            g_array_append_val(gruppo, t);
            trovati++;
        }
    }
    /* A gruppi di 10, come arriverebbero dal telefono. */
    for (guint i = 0; i < gruppo->len && !ph_servizi_lavoro_annullato(r->l); i += 10)
        ph_servizi_lavoro_trovati_dimensioni(r->l, &g_array_index(gruppo, PhSrvTrovato, i),
                                             &g_array_index(dimensioni, uint64_t, i), MIN(10u, gruppo->len - i));
    ph_servizi_lavoro_cerca_fine(r->l, PH_SRV_ESITO_OK, NULL, massimo);
    g_array_free(gruppo, TRUE);
    g_array_free(dimensioni, TRUE);
    g_ptr_array_unref(tenere);
    g_free(base);
    g_free(ago);
}

static void fai_crea(PhFinto *f, void *dati)
{
    Richiesta *r = dati;
    r->finito = true;
    char *p = normalizza(r->a);
    char *padre = g_path_get_dirname(p);
    char *nome = g_path_get_basename(p);
    char *ripulito = g_strdup(nome);
    if (!collegato(f))
        ph_servizi_lavoro_fatto(r->l, PH_SRV_ESITO_NON_COLLEGATO, "Telefono non collegato");
    else if (non_consentita(padre))
        ph_servizi_lavoro_fatto(r->l, PH_SRV_ESITO_NON_CONSENTITO, NULL);
    else if (cartella_di(f, padre, false) == NULL)
        ph_servizi_lavoro_fatto(r->l, PH_SRV_ESITO_NON_TROVATO, NULL);
    else if (cartella_di(f, p, false) != NULL)
        ph_servizi_lavoro_fatto(r->l, PH_SRV_ESITO_ERRORE, "La cartella esiste gia'");
    else {
        aggiungi_voce(f, padre, ripulito, true, 0);
        fprintf(stderr, "finto: creata una cartella\n");
        ph_servizi_lavoro_fatto(r->l, PH_SRV_ESITO_OK, NULL);
    }
    g_free(p);
    g_free(padre);
    g_free(nome);
    g_free(ripulito);
}

static bool esiste_file(PhFinto *f, const char *p)
{
    char *padre = g_path_get_dirname(p), *nome = g_path_get_basename(p);
    GPtrArray *a = cartella_di(f, padre, false);
    bool si = false;
    for (guint i = 0; a != NULL && i < a->len; i++) {
        const Voce *v = g_ptr_array_index(a, i);
        if (!v->cartella && strcmp(v->nome, nome) == 0)
            si = true;
    }
    g_free(padre);
    g_free(nome);
    return si;
}

static void fai_miniatura(PhFinto *f, void *dati)
{
    Richiesta *r = dati;
    r->finito = true;
    char *p = normalizza(r->a);
    if (!collegato(f) || !esiste_file(f, p)) {
        ph_servizi_lavoro_immagine(r->l, collegato(f) ? PH_SRV_ESITO_NON_TROVATO : PH_SRV_ESITO_NON_COLLEGATO,
                                   NULL, NULL, 0);
    } else {
        guint h = g_str_hash(p);
        GBytes *b = finto_png_sfumatura(0x1e3a5f + (h & 0x3f3f3f), 0xfde68a - (h & 0x1f1f1f), r->lato,
                                        r->lato * 3 / 4);
        gsize n = 0;
        const void *d = b != NULL ? g_bytes_get_data(b, &n) : NULL;
        ph_servizi_lavoro_immagine(r->l, b != NULL ? PH_SRV_ESITO_OK : PH_SRV_ESITO_ERRORE, NULL, d, n);
        if (b != NULL)
            g_bytes_unref(b);
    }
    g_free(p);
}

static void fai_leggi(PhFinto *f, void *dati)
{
    Richiesta *r = dati;
    r->finito = true;
    char *p = normalizza(r->a);
    if (!collegato(f) || !esiste_file(f, p)) {
        ph_servizi_lavoro_byte(r->l, collegato(f) ? PH_SRV_ESITO_NON_TROVATO : PH_SRV_ESITO_NON_COLLEGATO, NULL,
                               NULL);
    } else {
        /* Ogni file letto e' un'immagine generata (per lo sfondo). */
        guint h = g_str_hash(p);
        GBytes *b = finto_png_sfumatura(0x312e81 + (h & 0x1f1f1f), 0xfb923c, 1080, 1920);
        if (b != NULL && g_bytes_get_size(b) > r->massimo)
            ph_servizi_lavoro_byte(r->l, PH_SRV_ESITO_ERRORE, "File troppo grande", NULL);
        else
            ph_servizi_lavoro_byte(r->l, b != NULL ? PH_SRV_ESITO_OK : PH_SRV_ESITO_ERRORE, NULL, b);
        if (b != NULL)
            g_bytes_unref(b);
    }
    g_free(p);
}

static void fai_memoria(PhFinto *f, void *dati)
{
    Richiesta *r = dati;
    r->finito = true;
    if (!collegato(f))
        ph_servizi_lavoro_memoria(r->l, PH_SRV_ESITO_NON_COLLEGATO, 0, 0);
    else
        ph_servizi_lavoro_memoria(r->l, PH_SRV_ESITO_OK, 256ull << 30, 96ull << 30);
}

/* ---- Trasferimenti ----------------------------------------------------------- */
static void tr_libera(gpointer p)
{
    ph_srv_trasferimenti_libera(p, 1);
}

static gboolean ogni_300ms(gpointer p)
{
    PhFinto *f = p;
    if (!collegato(f))
        return G_SOURCE_CONTINUE;
    for (guint i = 0; i < f->tr->len; i++) {
        PhSrvTrasferimento *t = g_ptr_array_index(f->tr, i);
        if (t->stato != PH_SRV_TR_ATTESA && t->stato != PH_SRV_TR_CORSO)
            continue;
        /* Uno alla volta, nell'ordine; passo del mockup (xferTick). */
        t->stato = PH_SRV_TR_CORSO;
        uint64_t dim = t->dimensione > 0 ? t->dimensione : 1;
        int passo = (int)CLAMP((60.0 * 1048576.0) / (double)dim, 3.0, 25.0);
        uint64_t fatti = t->fatti + dim * (uint64_t)passo / 100u;
        if (fatti >= dim) {
            t->fatti = dim;
            t->stato = PH_SRV_TR_COMPLETATO;
            if (t->verso == PH_SRV_INVIO) {
                char *dir = g_path_get_dirname(t->destinazione);
                aggiungi_voce(f, dir, t->nome, false, dim);
                g_free(dir);
            }
            fprintf(stderr, "finto: trasferimento %llu completato\n", (unsigned long long)t->id);
        } else {
            t->fatti = fatti;
        }
        pubblica_tr(f);
        break;
    }
    return G_SOURCE_CONTINUE;
}

typedef struct {
    PhSrvTrasferimento *t;
} NuovoTr;

static void fai_accoda(PhFinto *f, void *dati)
{
    PhSrvTrasferimento *t = dati;
    g_ptr_array_add(f->tr, t);
    pubblica_tr(f);
}

static int finto_tr_accoda(void *b, PhSrvVerso verso, const char *origine, const char *cartella, PhErrore *e)
{
    PhFinto *f = b;
    uint64_t dim = 0;
    if (verso == PH_SRV_INVIO) {
        struct stat st;
        if (stat(origine, &st) != 0 || !S_ISREG(st.st_mode))
            return ph_errore(e, "Il file non esiste o non e' un file normale");
        dim = (uint64_t)st.st_size;
    } else {
        if (!g_file_test(cartella, G_FILE_TEST_IS_DIR))
            return ph_errore(e, "La cartella del PC non esiste");
        dim = 1048576; /* la dimensione vera la saprebbe il telefono */
    }
    PhSrvTrasferimento *t = calloc(1, sizeof *t);
    if (t == NULL)
        return ph_errore(e, "memoria finita");
    char *nome = g_path_get_basename(origine);
    char *dir = verso == PH_SRV_INVIO ? normalizza(cartella) : g_strdup(cartella);
    char *dest = unisci(dir, nome);
    t->id = ++f->prossimo_tr; /* letto solo qui e dal thread di lavoro dopo l'accodamento */
    t->verso = verso;
    t->stato = PH_SRV_TR_ATTESA;
    t->nome = strdup(nome);
    t->origine = strdup(origine);
    t->destinazione = strdup(dest);
    t->errore = strdup("");
    t->dimensione = dim;
    g_free(nome);
    g_free(dir);
    g_free(dest);
    finto_lavora(f, 0, fai_accoda, t, NULL);
    return 0;
}

static void fai_annulla(PhFinto *f, void *dati)
{
    uint64_t id = *(uint64_t *)dati;
    for (guint i = 0; i < f->tr->len; i++) {
        PhSrvTrasferimento *t = g_ptr_array_index(f->tr, i);
        if ((id == 0 || t->id == id) && (t->stato == PH_SRV_TR_ATTESA || t->stato == PH_SRV_TR_CORSO))
            t->stato = PH_SRV_TR_ANNULLATO;
    }
    pubblica_tr(f);
}

static void finto_tr_annulla(void *b, uint64_t id)
{
    if (id == 0)
        return;
    uint64_t *p = g_new(uint64_t, 1);
    *p = id;
    finto_lavora(b, 0, fai_annulla, p, g_free);
}

static void finto_tr_annulla_tutto(void *b)
{
    uint64_t *p = g_new0(uint64_t, 1);
    finto_lavora(b, 0, fai_annulla, p, g_free);
}

static void fai_rimuovi_finiti(PhFinto *f, void *dati)
{
    (void)dati;
    for (guint i = 0; i < f->tr->len;) {
        PhSrvTrasferimento *t = g_ptr_array_index(f->tr, i);
        if (t->stato == PH_SRV_TR_COMPLETATO || t->stato == PH_SRV_TR_FALLITO || t->stato == PH_SRV_TR_ANNULLATO)
            g_ptr_array_remove_index(f->tr, i);
        else
            i++;
    }
    pubblica_tr(f);
}

static void finto_tr_rimuovi_finiti(void *b)
{
    finto_lavora(b, 0, fai_rimuovi_finiti, NULL, NULL);
}

/* ---- Tabella del backend (thread GTK) --------------------------------------- */
static void finto_riconnetti(void *b) { finto_lavora(b, 0, fai_riconnetti, NULL, NULL); }

static void fai_pannello(PhFinto *f, void *dati)
{
    f->pannello = GPOINTER_TO_INT(dati) != 0;
    fprintf(stderr, "finto: pannello fisico %s\n", f->pannello ? "acceso" : "spento");
    pubblica_telefono(f);
}

static void finto_pannello(void *b, bool acceso) { finto_lavora(b, ritardo(b), fai_pannello, GINT_TO_POINTER(acceso), NULL); }

static void fai_volume(PhFinto *f, void *dati)
{
    f->volume = GPOINTER_TO_INT(dati);
    pubblica_volume(f);
}

static void fai_muto(PhFinto *f, void *dati)
{
    f->muto = GPOINTER_TO_INT(dati) != 0;
    pubblica_volume(f);
}

static void finto_volume(void *b, int v) { finto_lavora(b, 0, fai_volume, GINT_TO_POINTER(v), NULL); }
static void finto_muto(void *b, bool m) { finto_lavora(b, 0, fai_muto, GINT_TO_POINTER(m), NULL); }

static void finto_appunti(void *b, const char *testo)
{
    (void)b;
    fprintf(stderr, "finto: appunti dal PC (%zu byte)\n", strlen(testo));
}

typedef struct {
    char *pacchetto;
    int lato;
} IconaChiesta;

static void icona_libera(gpointer p)
{
    IconaChiesta *c = p;
    g_free(c->pacchetto);
    g_free(c);
}

static void fai_icona(PhFinto *f, void *dati)
{
    IconaChiesta *c = dati;
    const char *mono = "?";
    for (size_t i = 0; i < G_N_ELEMENTS(APP); i++)
        if (strcmp(APP[i].pacchetto, c->pacchetto) == 0)
            mono = APP[i].mono;
    GBytes *b = finto_png_icona(mono, finto_colore_app(c->pacchetto), c->lato);
    gsize n = 0;
    const void *d = b != NULL ? g_bytes_get_data(b, &n) : NULL;
    ph_servizi_pubblica_icona(f->s, c->pacchetto, d, n);
    if (b != NULL)
        g_bytes_unref(b);
}

static void finto_icona(void *b, const char *pacchetto, int lato)
{
    IconaChiesta *c = g_new(IconaChiesta, 1);
    c->pacchetto = g_strdup(pacchetto);
    c->lato = lato;
    finto_lavora(b, 30, fai_icona, c, icona_libera);
}

static void finto_elenca(void *b, PhSrvLavoro *l, const char *cartella)
{
    finto_lavora(b, ritardo(b), fai_elenca, richiesta(l, cartella, NULL), richiesta_libera);
}

static void finto_cerca(void *b, PhSrvLavoro *l, const char *cartella, const char *testo, bool solo_cartelle,
                        unsigned massimo)
{
    Richiesta *r = richiesta(l, cartella, testo);
    r->flag = solo_cartelle;
    r->numero = massimo;
    finto_lavora(b, ritardo(b) * 2, fai_cerca, r, richiesta_libera);
}

static void finto_crea(void *b, PhSrvLavoro *l, const char *percorso)
{
    finto_lavora(b, ritardo(b), fai_crea, richiesta(l, percorso, NULL), richiesta_libera);
}

static void finto_miniatura(void *b, PhSrvLavoro *l, const char *percorso, int lato)
{
    Richiesta *r = richiesta(l, percorso, NULL);
    r->lato = lato;
    finto_lavora(b, ritardo(b), fai_miniatura, r, richiesta_libera);
}

static void finto_leggi(void *b, PhSrvLavoro *l, const char *percorso, size_t massimo)
{
    Richiesta *r = richiesta(l, percorso, NULL);
    r->massimo = massimo;
    finto_lavora(b, ritardo(b) * 3, fai_leggi, r, richiesta_libera);
}

static void finto_memoria(void *b, PhSrvLavoro *l)
{
    finto_lavora(b, ritardo(b), fai_memoria, richiesta(l, NULL, NULL), richiesta_libera);
}

static void fai_salva_profilo(PhFinto *f, void *dati)
{
    ph_cfg_telefono_libera(f->profilo_salvato);
    f->profilo_salvato = dati;
    fprintf(stderr, "finto: profilo salvato (in memoria)\n");
}

static void finto_profilo_salva(void *b, PhCfgTelefono *copia)
{
    /* Il primo profilo (pubblicato da collegati) e' gia' "salvato": serve solo
     * a sapere che il telefono e' noto. */
    finto_lavora(b, 0, fai_salva_profilo, copia, NULL);
}

static void finto_preferenze_salva(void *b, PhCfgPreferenze *copia)
{
    (void)b;
    fprintf(stderr, "finto: preferenze salvate (in memoria)\n");
    free(copia);
}

static void ferma_ciclo(PhFinto *f, void *dati)
{
    (void)dati;
    g_main_loop_quit(f->loop);
}

static void finto_meteo_fine(PhFinto *f);

static void finto_chiudi(void *b)
{
    PhFinto *f = b;
    finto_lavora(f, 0, ferma_ciclo, NULL, NULL);
    g_thread_join(f->thread);
    f->thread = NULL;
    if (f->trova_src != NULL) {
        g_source_destroy(f->trova_src);
        g_source_unref(f->trova_src);
        f->trova_src = NULL;
    }
    finto_meteo_fine(f);
    g_free(f->meteo_ultimo);
    g_free(f->meteo_citta);
    /* Le sorgenti rimaste si distruggono qui: le loro funzioni di rilascio
     * chiudono i lavori ancora aperti (servizi_backend.h, LAVORI). */
    g_main_loop_unref(f->loop);
    g_main_context_unref(f->ctx);
    g_ptr_array_unref(f->notifiche);
    g_ptr_array_unref(f->tr);
    g_hash_table_destroy(f->albero);
    g_ptr_array_unref(f->flussi);
    g_hash_table_destroy(f->disinstallate);
    ph_cfg_telefono_libera(f->profilo_salvato);
    g_mutex_clear(&f->m);
    g_ptr_array_unref(f->ricerche);
    g_mutex_clear(&f->m_ric);
    g_free(f);
}

/* ---- Aggiunte del 23 nov ----------------------------------------------------- */

/* (1) Installazione: copia simulata in 10 passi da 200 ms (avanzamento,
 * annullamento durante la copia), poi l'esito. */
typedef struct {
    PhFinto *f;
    PhSrvLavoro *l;
    bool finito;
    char *apk;
    uint64_t totale;
    int passo;
} Installazione;

static void inst_finisci(Installazione *in, PhSrvInstEsito esito, const char *messaggio, const char *codice)
{
    if (in->finito)
        return;
    in->finito = true;
    PhSrvInstRisultato r = {esito, messaggio, codice != NULL ? codice : "", false};
    ph_servizi_lavoro_inst(in->l, &r);
}

static void inst_libera(gpointer p)
{
    Installazione *in = p;
    inst_finisci(in, PH_SRV_INST_ERRORE, "Installazione interrotta: il telefono finto si e' fermato.", NULL);
    g_free(in->apk);
    g_free(in);
}

static gboolean inst_passo(gpointer p)
{
    Installazione *in = p;
    PhFinto *f = in->f;
    if (atomic_load(ph_servizi_lavoro_flag_annulla(in->l))) {
        inst_finisci(in, PH_SRV_INST_ANNULLATO, "Installazione annullata.", NULL);
        return G_SOURCE_REMOVE;
    }
    if (!collegato(f)) {
        inst_finisci(in, PH_SRV_INST_ERRORE, "La connessione con il telefono si e' interrotta.", NULL);
        return G_SOURCE_REMOVE;
    }
    if (in->passo < 10) {
        in->passo++;
        ph_servizi_lavoro_avanzamento(in->l, in->totale * (uint64_t)in->passo / 10u, in->totale);
        return G_SOURCE_CONTINUE;
    }
    char *base = g_path_get_basename(in->apk);
    char *minu = g_ascii_strdown(base, -1);
    if (strstr(minu, "errore") != NULL)
        inst_finisci(in, PH_SRV_INST_ERRORE, "Il telefono non ha risposto: non si sa se l'app sia stata installata.",
                     NULL);
    else if (f->inst_rifiutate || strstr(minu, "rifiut") != NULL)
        inst_finisci(in, PH_SRV_INST_RIFIUTATO,
                     "L'app e' gia' installata ma firmata con un'altra chiave: disinstallala prima.",
                     "INSTALL_FAILED_UPDATE_INCOMPATIBLE");
    else
        inst_finisci(in, PH_SRV_INST_RIUSCITO, "App installata.", NULL);
    fprintf(stderr, "finto: installazione finita\n");
    g_free(base);
    g_free(minu);
    return G_SOURCE_REMOVE;
}

static void finto_apk_installa(void *b, PhSrvLavoro *l, const char *apk_pc)
{
    PhFinto *f = b;
    Installazione *in = g_new0(Installazione, 1);
    in->f = f;
    in->l = l;
    in->apk = g_strdup(apk_pc);
    struct stat st;
    in->totale = stat(apk_pc, &st) == 0 && S_ISREG(st.st_mode) ? (uint64_t)st.st_size : 0;
    if (in->totale == 0) {
        inst_finisci(in, PH_SRV_INST_ERRORE, "Il file da installare non si puo' leggere.", NULL);
        g_free(in->apk);
        g_free(in);
        return;
    }
    fprintf(stderr, "finto: installo un APK di %llu byte\n", (unsigned long long)in->totale);
    GSource *src = g_timeout_source_new(200);
    g_source_set_callback(src, inst_passo, in, inst_libera);
    g_source_attach(src, f->ctx);
    g_source_unref(src);
}

typedef struct {
    PhSrvLavoro *l;
    bool finito;
    char *pacchetto;
} Disinstallazione;

static void dis_libera(gpointer p)
{
    Disinstallazione *d = p;
    if (!d->finito) {
        PhSrvInstRisultato r = {PH_SRV_INST_ERRORE, "Disinstallazione interrotta.", "", false};
        ph_servizi_lavoro_inst(d->l, &r);
    }
    g_free(d->pacchetto);
    g_free(d);
}

static void fai_disinstalla(PhFinto *f, void *dati)
{
    Disinstallazione *d = dati;
    d->finito = true;
    const AppFinta *a = NULL;
    for (size_t i = 0; i < G_N_ELEMENTS(APP); i++)
        if (strcmp(APP[i].pacchetto, d->pacchetto) == 0 && !g_hash_table_contains(f->disinstallate, d->pacchetto))
            a = &APP[i];
    PhSrvInstRisultato r = {PH_SRV_INST_RIUSCITO, "App disinstallata.", "", false};
    if (!collegato(f)) {
        r = (PhSrvInstRisultato){PH_SRV_INST_ERRORE, "Nessun telefono collegato.", "", false};
    } else if (a == NULL) {
        r = (PhSrvInstRisultato){PH_SRV_INST_RIFIUTATO, "Questa app non e' installata sul telefono.",
                                 "DELETE_FAILED_INTERNAL_ERROR", false};
    } else if (a->sistema) {
        r = (PhSrvInstRisultato){PH_SRV_INST_RIFIUTATO, "E' un'app di sistema: il telefono non permette di toglierla.",
                                 "DELETE_FAILED_SYSTEM_APP", false};
    } else {
        g_hash_table_add(f->disinstallate, g_strdup(d->pacchetto));
        pubblica_app(f);
        fprintf(stderr, "finto: app disinstallata\n");
    }
    ph_servizi_lavoro_inst(d->l, &r);
}

static void finto_app_disinstalla(void *b, PhSrvLavoro *l, const char *pacchetto)
{
    Disinstallazione *d = g_new0(Disinstallazione, 1);
    d->l = l;
    d->pacchetto = g_strdup(pacchetto);
    finto_lavora(b, ritardo(b) * 3, fai_disinstalla, d, dis_libera);
}

/* (3) */
static void fai_annulla_verso(PhFinto *f, void *dati)
{
    PhSrvVerso verso = (PhSrvVerso)GPOINTER_TO_INT(dati);
    for (guint i = 0; i < f->tr->len; i++) {
        PhSrvTrasferimento *t = g_ptr_array_index(f->tr, i);
        if (t->verso == verso && (t->stato == PH_SRV_TR_ATTESA || t->stato == PH_SRV_TR_CORSO))
            t->stato = PH_SRV_TR_ANNULLATO;
    }
    pubblica_tr(f);
}

static void finto_tr_annulla_verso(void *b, PhSrvVerso verso)
{
    finto_lavora(b, 0, fai_annulla_verso, GINT_TO_POINTER((int)verso), NULL);
}

/* (4) */
typedef struct {
    PhSrvLavoro *l;
    bool finito, acceso;
} RichiestaPannello;

static void pannello_libera(gpointer p)
{
    RichiestaPannello *r = p;
    if (!r->finito)
        ph_servizi_lavoro_pannello(r->l, PH_SRV_PAN_ERRORE, "chiuso");
    g_free(r);
}

static void fai_pannello_chiedi(PhFinto *f, void *dati)
{
    RichiestaPannello *r = dati;
    r->finito = true;
    if (!collegato(f)) {
        ph_servizi_lavoro_pannello(r->l, PH_SRV_PAN_NON_COLLEGATO, NULL);
    } else if (f->bloccato) {
        ph_servizi_lavoro_pannello(r->l, PH_SRV_PAN_BLOCCATO, "Il telefono e' bloccato (simulato)");
    } else if (f->risposta_pannello != PH_SRV_PAN_FATTO) {
        fprintf(stderr, "finto: pannello rifiutato (%d)\n", (int)f->risposta_pannello);
        ph_servizi_lavoro_pannello(r->l, f->risposta_pannello, "Rifiuto simulato dal telefono finto");
    } else {
        f->pannello = r->acceso;
        fprintf(stderr, "finto: pannello fisico %s\n", f->pannello ? "acceso" : "spento");
        pubblica_telefono(f);
        ph_servizi_lavoro_pannello(r->l, PH_SRV_PAN_FATTO, NULL);
    }
}

static void finto_pannello_chiedi(void *b, PhSrvLavoro *l, bool acceso)
{
    RichiestaPannello *r = g_new0(RichiestaPannello, 1);
    r->l = l;
    r->acceso = acceso;
    finto_lavora(b, ritardo(b), fai_pannello_chiedi, r, pannello_libera);
}

/* (5) */
static void fai_dettaglio(PhFinto *f, void *dati)
{
    Richiesta *r = dati;
    r->finito = true;
    if (!collegato(f)) {
        ph_servizi_lavoro_dettaglio(r->l, PH_SRV_ESITO_NON_COLLEGATO, NULL, NULL);
        return;
    }
    /* Valori del mockup; le app "non leggibili" per provare quel caso. */
    PhSrvMemoriaDettaglio d = {{38ull << 30, true}, {54ull << 30, true}, {6ull << 30, true}, {0, false}};
    ph_servizi_lavoro_dettaglio(r->l, PH_SRV_ESITO_OK, NULL, &d);
}

static void finto_dettaglio(void *b, PhSrvLavoro *l)
{
    finto_lavora(b, ritardo(b), fai_dettaglio, richiesta(l, NULL, NULL), richiesta_libera);
}

static void fai_calendario(PhFinto *f, void *dati)
{
    Richiesta *r = dati;
    r->finito = true;
    if (!collegato(f)) {
        ph_servizi_lavoro_calendario(r->l, PH_SRV_ESITO_NON_COLLEGATO, NULL, NULL, 0);
        return;
    }
    if (f->cal_illeggibile) {
        ph_servizi_lavoro_calendario(r->l, PH_SRV_ESITO_ERRORE,
                                     "Il telefono non permette di leggere il calendario (simulato)", NULL, 0);
        return;
    }
    /* Gli eventi del mockup (oggi, ora locale) e uno di tutto il giorno domani
     * (mezzanotte UTC, come il contratto). */
    static const struct {
        int ora, minuti, durata_min;
        const char *titolo;
    } EV[] = {{9, 30, 60, "Team meeting"}, {13, 0, 60, "Lunch"}, {15, 30, 30, "Client call"},
              {18, 30, 90, "Gym"}, {20, 30, 120, "Dinner"}};
    PhSrvEventoCalendario v[G_N_ELEMENTS(EV) + 1];
    GDateTime *ora = g_date_time_new_now_local();
    size_t n = 0;
    for (size_t i = 0; i < G_N_ELEMENTS(EV) && n < r->numero; i++) {
        GDateTime *t = g_date_time_new_local(g_date_time_get_year(ora), g_date_time_get_month(ora),
                                             g_date_time_get_day_of_month(ora), EV[i].ora, EV[i].minuti, 0);
        uint64_t ini = (uint64_t)g_date_time_to_unix(t) * 1000u;
        g_date_time_unref(t);
        v[n++] = (PhSrvEventoCalendario){EV[i].titolo, ini, ini + (uint64_t)EV[i].durata_min * 60000u, false};
    }
    if (n < r->numero) {
        GDateTime *u = g_date_time_new_now_utc();
        GDateTime *m = g_date_time_new_utc(g_date_time_get_year(u), g_date_time_get_month(u),
                                           g_date_time_get_day_of_month(u), 0, 0, 0);
        uint64_t domani = ((uint64_t)g_date_time_to_unix(m) + 86400u) * 1000u;
        g_date_time_unref(u);
        g_date_time_unref(m);
        v[n++] = (PhSrvEventoCalendario){"Birthday party", domani, domani + 86400000u, true};
    }
    g_date_time_unref(ora);
    ph_servizi_lavoro_calendario(r->l, PH_SRV_ESITO_OK, NULL, v, n);
}

static void finto_calendario(void *b, PhSrvLavoro *l, unsigned massimo)
{
    Richiesta *r = richiesta(l, NULL, NULL);
    r->numero = massimo;
    finto_lavora(b, ritardo(b), fai_calendario, r, richiesta_libera);
}

/* ---- Widget del 23 nov pomeriggio -------------------------------------------- */

/* Richiesta dei lavori con risposte di tipo diverso da PhSrvFatto: se il thread
 * si ferma prima, il lavoro si chiude comunque con la sua funzione. */
typedef enum { RW_CAL_MESE, RW_SVEGLIA, RW_BATTERIA } TipoRw;
typedef struct {
    PhSrvLavoro *l;
    bool finito;
    TipoRw tipo;
    uint64_t da_ms, a_ms;
} RichiestaW;

static RichiestaW *richiesta_w(PhSrvLavoro *l, TipoRw tipo)
{
    RichiestaW *r = g_new0(RichiestaW, 1);
    r->l = l;
    r->tipo = tipo;
    return r;
}

static void richiesta_w_libera(gpointer p)
{
    RichiestaW *r = p;
    if (!r->finito) {
        switch (r->tipo) {
        case RW_CAL_MESE:
            ph_servizi_lavoro_calendario_intervallo(r->l, PH_SRV_ESITO_ERRORE, "chiuso", NULL, 0, false, NULL);
            break;
        case RW_SVEGLIA:
            ph_servizi_lavoro_sveglia(r->l, PH_SRV_ESITO_ERRORE, "chiuso", false, 0, NULL);
            break;
        case RW_BATTERIA:
            ph_servizi_lavoro_batteria(r->l, PH_SRV_ESITO_ERRORE, "chiuso", NULL);
            break;
        }
    }
    g_free(r);
}

/* Calendario su un intervallo. Per ogni giorno locale: feriali con 0, 2, 5 (giorno
 * pieno), 1 o 3 eventi secondo il giorno del mese; sabati con un evento nei
 * giorni dispari; ogni domenica un «tutto il giorno» (mezzanotte UTC); inoltre
 * una fiera di due giorni interi il 5 e una trasferta su tre giorni (12-14, ore
 * locali) in ogni mese. */
typedef struct {
    int ora, minuti, durata_min;
    const char *titolo;
} EventoTipo;

static const EventoTipo EV_GIORNO[] = {{9, 30, 60, "Team meeting"}, {13, 0, 60, "Lunch"},
                                       {15, 30, 30, "Client call"}, {18, 30, 90, "Gym"},
                                       {20, 30, 120, "Dinner"}};

static int confronta_eventi(const void *a, const void *b)
{
    const PhSrvEventoCalendario *x = a, *y = b;
    return x->inizio_ms < y->inizio_ms ? -1 : x->inizio_ms > y->inizio_ms;
}

static uint64_t locale_ms(int anno, int mese, int giorno, int ora, int minuti)
{
    GDateTime *t = g_date_time_new_local(anno, mese, giorno, ora, minuti, 0);
    uint64_t ms = (uint64_t)g_date_time_to_unix(t) * 1000u;
    g_date_time_unref(t);
    return ms;
}

static uint64_t utc_ms(int anno, int mese, int giorno)
{
    GDateTime *t = g_date_time_new_utc(anno, mese, giorno, 0, 0, 0);
    uint64_t ms = (uint64_t)g_date_time_to_unix(t) * 1000u;
    g_date_time_unref(t);
    return ms;
}

static void fai_calendario_mese(PhFinto *f, void *dati)
{
    RichiestaW *r = dati;
    r->finito = true;
    if (!collegato(f)) {
        ph_servizi_lavoro_calendario_intervallo(r->l, PH_SRV_ESITO_NON_COLLEGATO, NULL, NULL, 0, false, NULL);
        return;
    }
    PhFintoModo m = f->modo[PH_FINTO_VOCE_CALENDARIO_MESE];
    if (f->cal_illeggibile || m == PH_FINTO_MODO_ERRORE) {
        ph_servizi_lavoro_calendario_intervallo(r->l, PH_SRV_ESITO_ERRORE,
                                                "Il telefono non permette di leggere il calendario (simulato)",
                                                NULL, 0, false, NULL);
        return;
    }
    if (m == PH_FINTO_MODO_NON_DISPONIBILE) {
        ph_servizi_lavoro_calendario_intervallo(r->l, PH_SRV_ESITO_ERRORE, NULL, NULL, 0, false, NULL);
        return;
    }
    /* Si parte tre giorni prima: gli eventi lunghi iniziati prima dell'intervallo lo toccano. */
    GDateTime *da = g_date_time_new_from_unix_local((gint64)(r->da_ms / 1000u));
    GDateTime *giorno = da != NULL ? g_date_time_add_days(da, -3) : NULL;
    if (da != NULL)
        g_date_time_unref(da);
    if (giorno == NULL) { /* data fuori dai limiti di GDateTime (anno 1-9999) */
        ph_servizi_lavoro_calendario_intervallo(r->l, PH_SRV_ESITO_ERRORE, "Data non rappresentabile (simulato)",
                                                NULL, 0, false, NULL);
        return;
    }
    GArray *v = g_array_new(FALSE, FALSE, sizeof(PhSrvEventoCalendario));
    int giorni = (int)((r->a_ms - r->da_ms) / 86400000u) + 5;
    for (int i = 0; i < giorni; i++) {
        int y = g_date_time_get_year(giorno), mo = g_date_time_get_month(giorno);
        int d = g_date_time_get_day_of_month(giorno), sett = g_date_time_get_day_of_week(giorno);
        int quanti = 0;
        if (sett <= 5) {
            static const int PER_GIORNO[5] = {0, 2, 5, 1, 3};
            quanti = PER_GIORNO[d % 5];
        } else if (sett == 6) {
            quanti = d % 2;
        }
        for (int k = 0; k < quanti; k++) {
            /* Il sabato l'unico evento e' la cena; nei feriali i primi `quanti` del mockup. */
            const EventoTipo *e = sett == 6 ? &EV_GIORNO[4] : &EV_GIORNO[k];
            uint64_t ini = locale_ms(y, mo, d, e->ora, e->minuti);
            PhSrvEventoCalendario ev = {e->titolo, ini, ini + (uint64_t)e->durata_min * 60000u, false};
            g_array_append_val(v, ev);
        }
        if (sett == 7) {
            uint64_t ini = utc_ms(y, mo, d);
            PhSrvEventoCalendario ev = {"Family lunch", ini, ini + 86400000u, true};
            g_array_append_val(v, ev);
        }
        if (d == 5) {
            uint64_t ini = utc_ms(y, mo, d);
            PhSrvEventoCalendario ev = {"Book fair", ini, ini + 2u * 86400000u, true};
            g_array_append_val(v, ev);
        }
        if (d == 12) {
            PhSrvEventoCalendario ev = {"Business trip", locale_ms(y, mo, d, 8, 0),
                                        locale_ms(y, mo, d + 2, 18, 0), false};
            g_array_append_val(v, ev);
        }
        GDateTime *dopo = g_date_time_add_days(giorno, 1);
        g_date_time_unref(giorno);
        giorno = dopo;
        if (giorno == NULL)
            break; /* fine del calendario rappresentabile */
    }
    if (giorno != NULL)
        g_date_time_unref(giorno);
    /* Solo quelli che toccano [da, a), in ordine di inizio. */
    PhSrvEventoCalendario *out = g_new(PhSrvEventoCalendario, v->len + 1);
    size_t n = 0;
    for (guint i = 0; i < v->len; i++) {
        const PhSrvEventoCalendario *e = &g_array_index(v, PhSrvEventoCalendario, i);
        if (e->fine_ms > r->da_ms && e->inizio_ms < r->a_ms)
            out[n++] = *e;
    }
    g_array_free(v, TRUE);
    qsort(out, n, sizeof *out, confronta_eventi);
    size_t massimo = f->cal_massimo > 0 ? (size_t)f->cal_massimo : PH_SRV_CALENDARIO_INTERVALLO_MAX;
    bool troncato = n > massimo;
    if (troncato)
        n = massimo;
    ph_servizi_lavoro_calendario_intervallo(r->l, PH_SRV_ESITO_OK, NULL, out, n, troncato, "it.finto.calendario");
    g_free(out);
}

static void finto_calendario_intervallo(void *b, PhSrvLavoro *l, uint64_t da_ms, uint64_t a_ms)
{
    RichiestaW *r = richiesta_w(l, RW_CAL_MESE);
    r->da_ms = da_ms;
    r->a_ms = a_ms;
    finto_lavora(b, ritardo(b), fai_calendario_mese, r, richiesta_w_libera);
}

/* Se la voce deve rispondere in modo anomalo lo dice e torna true. Per sveglia e batteria. */
static bool voce_anomala(PhFinto *f, PhFintoVoce voce, PhSrvEsito *esito, const char **frase)
{
    *frase = NULL;
    if (!collegato(f)) {
        *esito = PH_SRV_ESITO_NON_COLLEGATO;
        return true;
    }
    if (f->modo[voce] == PH_FINTO_MODO_NON_DISPONIBILE) {
        *esito = PH_SRV_ESITO_ERRORE;
        return true;
    }
    if (f->modo[voce] == PH_FINTO_MODO_ERRORE) {
        *esito = PH_SRV_ESITO_ERRORE;
        *frase = "Il telefono non risponde (simulato)";
        return true;
    }
    return false;
}

static void fai_sveglia(PhFinto *f, void *dati)
{
    RichiestaW *r = dati;
    r->finito = true;
    PhSrvEsito esito = PH_SRV_ESITO_OK;
    const char *frase;
    if (voce_anomala(f, PH_FINTO_VOCE_SVEGLIA, &esito, &frase)) {
        ph_servizi_lavoro_sveglia(r->l, esito, frase, false, 0, NULL);
        return;
    }
    if (f->sveglia_assente) {
        ph_servizi_lavoro_sveglia(r->l, PH_SRV_ESITO_OK, NULL, false, 0, "");
        return;
    }
    GDateTime *adesso = g_date_time_new_now_local();
    GDateTime *t = g_date_time_add_days(adesso, 1);
    uint64_t ora = locale_ms(g_date_time_get_year(t), g_date_time_get_month(t), g_date_time_get_day_of_month(t), 7, 30);
    g_date_time_unref(t);
    g_date_time_unref(adesso);
    ph_servizi_lavoro_sveglia(r->l, PH_SRV_ESITO_OK, NULL, true, ora, "it.finto.orologio");
}

static void finto_sveglia(void *b, PhSrvLavoro *l)
{
    finto_lavora(b, ritardo(b), fai_sveglia, richiesta_w(l, RW_SVEGLIA), richiesta_w_libera);
}

static void fai_batteria_dettaglio(PhFinto *f, void *dati)
{
    RichiestaW *r = dati;
    r->finito = true;
    PhSrvEsito esito = PH_SRV_ESITO_OK;
    const char *frase;
    if (voce_anomala(f, PH_FINTO_VOCE_BATTERIA, &esito, &frase)) {
        ph_servizi_lavoro_batteria(r->l, esito, frase, NULL);
        return;
    }
    PhSrvBatteria bat = {
        .livello = f->batteria,
        .stato = PH_SRV_BAT_IN_SCARICA,
        .alimentazione = 0,
        .temperatura_nota = true,
        .temperatura_decimi = 315,
        .salute = PH_SRV_SALUTE_BUONA,
        .tensione_mv = 4100,
        .minuti_carica = -1,
        .minuti_autonomia = 540,
    };
    if (f->in_carica) {
        bat.stato = f->batteria >= 100 ? PH_SRV_BAT_PIENA : PH_SRV_BAT_IN_CARICA;
        bat.alimentazione = PH_SRV_ALIM_RETE;
        bat.minuti_carica = f->batteria >= 100 ? 0 : 45;
        bat.minuti_autonomia = -1;
    }
    ph_servizi_lavoro_batteria(r->l, PH_SRV_ESITO_OK, NULL, &bat);
}

static void finto_batteria(void *b, PhSrvLavoro *l)
{
    finto_lavora(b, ritardo(b), fai_batteria_dettaglio, richiesta_w(l, RW_BATTERIA), richiesta_w_libera);
}

/* Controlli rapidi. */
static void fai_osserva(PhFinto *f, void *dati)
{
    f->ctl_osservati = GPOINTER_TO_INT(dati) != 0;
    pubblica_controlli(f);
}

static void finto_controlli_osserva(void *b, bool osserva)
{
    finto_lavora(b, 0, fai_osserva, GINT_TO_POINTER(osserva), NULL);
}

/* Anomalie comuni a comandi e «trova»: true se il lavoro e' gia' chiuso. */
static bool controllo_anomalo(PhFinto *f, PhSrvLavoro *l)
{
    if (ph_servizi_lavoro_annullato(l)) {
        /* Nessuna modifica; il lavoro si chiude lo stesso (l'esito non arriva a nessuno). */
        ph_servizi_lavoro_fatto(l, PH_SRV_ESITO_ERRORE, "annullato");
        return true;
    }
    if (!collegato(f)) {
        ph_servizi_lavoro_fatto(l, PH_SRV_ESITO_NON_COLLEGATO, NULL);
        return true;
    }
    if (f->modo[PH_FINTO_VOCE_CONTROLLI] == PH_FINTO_MODO_NON_DISPONIBILE) {
        ph_servizi_lavoro_fatto(l, PH_SRV_ESITO_ERRORE, NULL);
        return true;
    }
    if (f->modo[PH_FINTO_VOCE_CONTROLLI] == PH_FINTO_MODO_ERRORE) {
        ph_servizi_lavoro_fatto(l, PH_SRV_ESITO_ERRORE, "Il telefono rifiuta il comando (simulato)");
        return true;
    }
    return false;
}

static void fai_controllo(PhFinto *f, void *dati)
{
    Richiesta *r = dati;
    r->finito = true;
    if (controllo_anomalo(f, r->l))
        return;
    int valore = (int)r->numero;
    switch ((PhSrvControllo)r->lato) {
    case PH_SRV_CTL_SUONERIA:
        f->suoneria = valore;
        break;
    case PH_SRV_CTL_NON_DISTURBARE:
        f->non_disturbare = valore;
        break;
    case PH_SRV_CTL_TORCIA:
        if (f->torcia_manca) {
            ph_servizi_lavoro_fatto(r->l, PH_SRV_ESITO_ERRORE, NULL);
            return;
        }
        f->torcia = valore;
        break;
    }
    fprintf(stderr, "finto: controllo %d = %d\n", r->lato, valore);
    pubblica_controlli(f); /* prima dell'esito, stesso thread */
    ph_servizi_lavoro_fatto(r->l, PH_SRV_ESITO_OK, NULL);
}

static void finto_controllo_imposta(void *b, PhSrvLavoro *l, PhSrvControllo controllo, int valore)
{
    Richiesta *r = richiesta(l, NULL, NULL);
    r->lato = (int)controllo;
    r->numero = (unsigned)valore;
    finto_lavora(b, ritardo(b), fai_controllo, r, richiesta_libera);
}

/* Trova il telefono: si ferma da solo dopo i secondi chiesti. */
static void trova_ferma_interno(PhFinto *f)
{
    if (f->trova_src != NULL) {
        g_source_destroy(f->trova_src);
        g_source_unref(f->trova_src);
        f->trova_src = NULL;
    }
    if (f->trova_attivo) {
        f->trova_attivo = false;
        fprintf(stderr, "finto: trova il telefono fermato\n");
        pubblica_controlli(f);
    }
}

static gboolean trova_scaduta(gpointer p)
{
    PhFinto *f = p;
    /* La sorgente sta per sparire da sola: resta solo da lasciare il nostro riferimento. */
    g_source_unref(f->trova_src);
    f->trova_src = NULL;
    f->trova_attivo = false;
    fprintf(stderr, "finto: trova il telefono: limite raggiunto\n");
    pubblica_controlli(f);
    return G_SOURCE_REMOVE;
}

static void fai_trova_avvia(PhFinto *f, void *dati)
{
    Richiesta *r = dati;
    r->finito = true;
    if (controllo_anomalo(f, r->l))
        return;
    trova_ferma_interno(f); /* un secondo avvio riparte da capo */
    f->trova_attivo = true;
    f->trova_src = g_timeout_source_new_seconds(r->numero);
    g_source_set_callback(f->trova_src, trova_scaduta, f, NULL);
    g_source_attach(f->trova_src, f->ctx);
    fprintf(stderr, "finto: trova il telefono per %u s\n", r->numero);
    pubblica_controlli(f);
    ph_servizi_lavoro_fatto(r->l, PH_SRV_ESITO_OK, NULL);
}

static void finto_trova_avvia(void *b, PhSrvLavoro *l, int secondi)
{
    Richiesta *r = richiesta(l, NULL, NULL);
    r->numero = (unsigned)secondi;
    finto_lavora(b, ritardo(b), fai_trova_avvia, r, richiesta_libera);
}

static void fai_trova_ferma(PhFinto *f, void *dati)
{
    (void)dati;
    trova_ferma_interno(f);
}

static void finto_trova_ferma(void *b) { finto_lavora(b, 0, fai_trova_ferma, NULL, NULL); }

static void fai_voce_modo(PhFinto *f, void *dati)
{
    int v = GPOINTER_TO_INT(dati);
    f->modo[v / 4] = (PhFintoModo)(v % 4);
    if (v / 4 == PH_FINTO_VOCE_CONTROLLI)
        pubblica_controlli(f);
}

static void fai_sveglia_assente(PhFinto *f, void *dati) { f->sveglia_assente = GPOINTER_TO_INT(dati) != 0; }

static void fai_in_carica(PhFinto *f, void *dati)
{
    f->in_carica = GPOINTER_TO_INT(dati) != 0;
    pubblica_telefono(f);
}

static void fai_torcia_disponibile(PhFinto *f, void *dati)
{
    f->torcia_manca = GPOINTER_TO_INT(dati) == 0;
    pubblica_controlli(f);
}

static void fai_dal_telefono(PhFinto *f, void *dati)
{
    int v = GPOINTER_TO_INT(dati); /* controllo * 16 + valore */
    switch ((PhSrvControllo)(v / 16)) {
    case PH_SRV_CTL_SUONERIA:
        f->suoneria = v % 16;
        break;
    case PH_SRV_CTL_NON_DISTURBARE:
        f->non_disturbare = v % 16;
        break;
    case PH_SRV_CTL_TORCIA:
        f->torcia = v % 16;
        break;
    }
    pubblica_controlli(f);
}

void ph_finto_voce_modo(PhFinto *f, PhFintoVoce voce, PhFintoModo modo)
{
    if ((int)voce < 0 || voce >= PH_FINTO_VOCE_NUMERO || (int)modo < 0 || (int)modo > PH_FINTO_MODO_ERRORE)
        return;
    finto_lavora(f, 0, fai_voce_modo, GINT_TO_POINTER((int)voce * 4 + (int)modo), NULL);
}
static void fai_cal_massimo(PhFinto *f, void *dati) { f->cal_massimo = GPOINTER_TO_INT(dati); }
void ph_finto_calendario_massimo(PhFinto *f, int massimo)
{
    finto_lavora(f, 0, fai_cal_massimo, GINT_TO_POINTER(MAX(massimo, 0)), NULL);
}
void ph_finto_sveglia_assente(PhFinto *f, bool si) { finto_lavora(f, 0, fai_sveglia_assente, GINT_TO_POINTER(si), NULL); }
void ph_finto_in_carica(PhFinto *f, bool si) { finto_lavora(f, 0, fai_in_carica, GINT_TO_POINTER(si), NULL); }
void ph_finto_torcia_disponibile(PhFinto *f, bool si) { finto_lavora(f, 0, fai_torcia_disponibile, GINT_TO_POINTER(si), NULL); }
void ph_finto_controllo_dal_telefono(PhFinto *f, PhSrvControllo controllo, int valore)
{
    if (valore < 0 || valore > 15)
        return;
    finto_lavora(f, 0, fai_dal_telefono, GINT_TO_POINTER((int)controllo * 16 + valore), NULL);
}

/* ---- Meteo finto (CONTRATTI.md §18): nessuna rete, nessuna coordinata ------------ */

static void sorgente_ferma(GSource **s)
{
    if (*s != NULL) {
        g_source_destroy(*s);
        g_source_unref(*s);
        *s = NULL;
    }
}

/* Previsione finta secondo lo scenario (vedi finto.h) e il numero d'arrivo. */
static void meteo_genera(PhFinto *f, PhSrvMeteo *m)
{
    static const int CODICI_ORE[] = {0, 1, 2, 3, 45, 51, 61, 63, 71, 80, 95};
    static const int CODICI_GIORNI[] = {0, 3, 61, 73, 95, 45, 2};
    static const struct {
        double temp, percepita, umidita, prec, vento;
        int codice, giorno;
    } S[PH_FINTO_METEO_SCENARI] = {
        {14.5, 13.2, 62, 0.0, 12, 2, 1},   {6.0, 3.5, 81, 0.0, 5, 0, 0},     {11.0, 9.0, 93, 1.8, 22, 63, 1},
        {-4.5, -9.0, 88, 0.6, 18, 73, 1},  {24.0, 25.5, 78, 6.5, 40, 95, 1}, {3.0, 1.0, 99, 0.0, 3, 45, 0},
        {NAN, NAN, NAN, NAN, NAN, -1, -1},
    };
    int sc = CLAMP(f->meteo_scenario, 0, PH_FINTO_METEO_SCENARI - 1);
    int64_t adesso = g_get_real_time() / 1000000;
    memset(m, 0, sizeof *m);
    m->stato = PH_SRV_METEO_PRONTO;
    m->ha_dati = true;
    g_strlcpy(m->localita, "Sample City", sizeof m->localita);
    PhMeteoPrevisione *p = &m->dati;
    p->scarto_utc_s = 3600;
    p->ricevuta = adesso;
    double base = isnan(S[sc].temp) ? NAN : S[sc].temp + 0.1 * f->meteo_seq;
    p->adesso = (PhMeteoAdesso){adesso, base, S[sc].percepita, S[sc].umidita, S[sc].prec, S[sc].vento,
                                S[sc].codice, S[sc].giorno};
    p->n_ore = PH_METEO_ORE_MAX;
    double bo = sc == 6 ? 8.0 : base; /* nello scenario 6 le ore sono note, tranne qualche buco */
    int64_t inizio_ora = adesso - adesso % 3600;
    for (size_t i = 0; i < p->n_ore; i++) {
        int h = (int)((inizio_ora / 3600 + (int64_t)i) % 24); /* ora UTC */
        int salita = (int)(i % 24) < 12 ? (int)(i % 24) : 24 - (int)(i % 24);
        PhMeteoOra *o = &p->ore[i];
        o->ora = inizio_ora + (int64_t)i * 3600;
        o->temperatura = bo - 3.0 + 0.5 * salita;
        o->prob_pioggia = (double)((i * 7) % 100);
        o->codice = CODICI_ORE[(i + (size_t)sc) % G_N_ELEMENTS(CODICI_ORE)];
        o->giorno = h >= 6 && h < 20 ? 1 : 0;
        if (sc == 6) { /* un po' di valori non noti anche qui */
            if (i % 3 == 0)
                o->temperatura = NAN;
            if (i % 4 == 0)
                o->codice = -1;
            if (i % 5 == 0)
                o->giorno = -1;
        }
    }
    p->n_giorni = PH_METEO_GIORNI_MAX;
    int64_t mezzanotte = (adesso + p->scarto_utc_s) / 86400 * 86400 - p->scarto_utc_s;
    for (size_t k = 0; k < p->n_giorni; k++) {
        PhMeteoGiorno *g = &p->giorni[k];
        g->giorno = mezzanotte + (int64_t)k * 86400;
        g->codice = CODICI_GIORNI[(k + (size_t)sc) % G_N_ELEMENTS(CODICI_GIORNI)];
        g->minima = isnan(base) ? NAN : base - 4.0 + (double)(k % 3);
        g->massima = isnan(base) ? NAN : base + 3.0 + (double)(k % 4);
        if (g->codice == 73 && !isnan(base))
            g->minima = -3.0; /* neve: sotto zero */
        g->prob_pioggia = isnan(base) ? NAN : (double)(((k + 1) * 23) % 100);
        g->alba = isnan(base) ? 0 : g->giorno + 7 * 3600 + (int64_t)k * 60;
        g->tramonto = isnan(base) ? 0 : g->giorno + 17 * 3600 - (int64_t)k * 60;
    }
}

/* Pubblica lo stato `eff`; a_mano: con PRONTO, i dati sono quelli della citta' scelta. */
static void meteo_emetti(PhFinto *f, PhFintoMeteoModo eff, bool a_mano)
{
    PhSrvMeteo m;
    PhSrvMeteoFonte fonte = PH_SRV_METEO_FONTE_NESSUNA;
    if (eff == PH_FINTO_METEO_PRONTO) {
        meteo_genera(f, &m);
        fonte = a_mano ? PH_SRV_METEO_FONTE_A_MANO : PH_SRV_METEO_FONTE_TELEFONO;
        if (a_mano)
            g_strlcpy(m.localita, f->meteo_citta->nome, sizeof m.localita); /* titolo coerente con la citta' */
        if (f->meteo_ultimo == NULL)
            f->meteo_ultimo = g_new(PhSrvMeteo, 1);
        *f->meteo_ultimo = m;
        f->meteo_ultima_fonte = fonte;
    } else {
        memset(&m, 0, sizeof m);
        if (f->meteo_ultimo != NULL) { /* dati vecchi, con la loro ora e la loro fonte */
            m = *f->meteo_ultimo;
            fonte = f->meteo_ultima_fonte;
        }
        switch (eff) {
        case PH_FINTO_METEO_IN_ATTESA:
            m.stato = PH_SRV_METEO_IN_ATTESA;
            break;
        case PH_FINTO_METEO_NON_DISPONIBILE:
            m.stato = PH_SRV_METEO_NON_DISPONIBILE;
            g_strlcpy(m.frase, "Il servizio delle previsioni non risponde (simulato)", sizeof m.frase);
            break;
        case PH_FINTO_METEO_SENZA_POSIZIONE:
            m.stato = PH_SRV_METEO_SENZA_POSIZIONE;
            g_strlcpy(m.frase, "La posizione del telefono e' spenta (simulato)", sizeof m.frase);
            break;
        case PH_FINTO_METEO_NON_COLLEGATO:
            m.stato = PH_SRV_METEO_NON_COLLEGATO;
            g_strlcpy(m.frase, "Nessun telefono collegato (simulato)", sizeof m.frase);
            break;
        case PH_FINTO_METEO_PRONTO:
            break;
        }
    }
    ph_servizi_pubblica_meteo_fonte(f->s, &m, fonte);
}

/* Pubblica lo stato secondo il modo e il collegamento; solo mentre qualcuno osserva.
 * Precedenza (CONTRATTI.md §18): la posizione del telefono vince; la citta' scelta a
 * mano vale solo quando il telefono non da' una posizione (spenta, nessun telefono). */
static void meteo_pubblica(PhFinto *f)
{
    if (!f->meteo_osservato)
        return;
    PhFintoMeteoModo eff = collegato(f) ? f->meteo_modo : PH_FINTO_METEO_NON_COLLEGATO;
    bool a_mano = false;
    if ((eff == PH_FINTO_METEO_SENZA_POSIZIONE || eff == PH_FINTO_METEO_NON_COLLEGATO) && f->meteo_citta != NULL) {
        eff = PH_FINTO_METEO_PRONTO;
        a_mano = true;
    }
    meteo_emetti(f, eff, a_mano);
}

static gboolean meteo_primo_scaduto(gpointer p)
{
    PhFinto *f = p;
    g_source_unref(f->meteo_primo); /* la sorgente sparisce da sola */
    f->meteo_primo = NULL;
    meteo_pubblica(f);
    return G_SOURCE_REMOVE;
}

static gboolean meteo_ciclo_scaduto(gpointer p)
{
    meteo_pubblica(p);
    return G_SOURCE_CONTINUE;
}

static void meteo_avvia_ciclo(PhFinto *f)
{
    sorgente_ferma(&f->meteo_ciclo);
    f->meteo_ciclo = g_timeout_source_new_seconds((guint)(f->meteo_intervallo_s > 0 ? f->meteo_intervallo_s : 1800));
    g_source_set_callback(f->meteo_ciclo, meteo_ciclo_scaduto, f, NULL);
    g_source_attach(f->meteo_ciclo, f->ctx);
}

static void finto_meteo_fine(PhFinto *f)
{
    sorgente_ferma(&f->meteo_primo);
    sorgente_ferma(&f->meteo_ciclo);
    f->meteo_osservato = false;
}

/* Telefono tornato: la prima risposta riparte (IN_ATTESA con i dati vecchi, poi i nuovi). */
static void meteo_riparti(PhFinto *f)
{
    if (!f->meteo_osservato)
        return;
    sorgente_ferma(&f->meteo_primo);
    f->meteo_primo = g_timeout_source_new(f->meteo_lento ? 10000u : 300u);
    g_source_set_callback(f->meteo_primo, meteo_primo_scaduto, f, NULL);
    g_source_attach(f->meteo_primo, f->ctx);
    meteo_emetti(f, PH_FINTO_METEO_IN_ATTESA, false);
}

static void fai_meteo_osserva(PhFinto *f, void *dati)
{
    finto_meteo_fine(f);
    if (GPOINTER_TO_INT(dati) == 0) {
        fprintf(stderr, "finto: meteo spento\n");
        return;
    }
    f->meteo_osservato = true;
    f->meteo_primo = g_timeout_source_new(f->meteo_lento ? 10000u : 300u);
    g_source_set_callback(f->meteo_primo, meteo_primo_scaduto, f, NULL);
    g_source_attach(f->meteo_primo, f->ctx);
    meteo_avvia_ciclo(f);
    fprintf(stderr, "finto: meteo acceso\n");
}

static void finto_meteo_osserva(void *b, bool osserva)
{
    finto_lavora(b, 0, fai_meteo_osserva, GINT_TO_POINTER(osserva), NULL);
}

/* Dopo la prima risposta ogni cambio si vede subito; prima, aspetta la prima. */
static void meteo_cambiato(PhFinto *f)
{
    if (f->meteo_osservato && f->meteo_primo == NULL)
        meteo_pubblica(f);
}

static void fai_meteo_modo(PhFinto *f, void *dati)
{
    f->meteo_modo = (PhFintoMeteoModo)GPOINTER_TO_INT(dati);
    meteo_cambiato(f);
}

static void fai_meteo_scenario(PhFinto *f, void *dati)
{
    f->meteo_scenario = GPOINTER_TO_INT(dati);
    meteo_cambiato(f);
}

static void fai_meteo_nuovi(PhFinto *f, void *dati)
{
    (void)dati;
    f->meteo_seq++;
    meteo_cambiato(f);
}

static void fai_meteo_lento(PhFinto *f, void *dati) { f->meteo_lento = GPOINTER_TO_INT(dati) != 0; }

static void fai_meteo_intervallo(PhFinto *f, void *dati)
{
    f->meteo_intervallo_s = GPOINTER_TO_INT(dati);
    if (f->meteo_osservato)
        meteo_avvia_ciclo(f);
}

void ph_finto_meteo_modo(PhFinto *f, PhFintoMeteoModo modo)
{
    if ((int)modo < 0 || modo > PH_FINTO_METEO_NON_COLLEGATO)
        return;
    finto_lavora(f, 0, fai_meteo_modo, GINT_TO_POINTER((int)modo), NULL);
}
void ph_finto_meteo_condizioni(PhFinto *f, int scenario)
{
    if (scenario < 0 || scenario >= PH_FINTO_METEO_SCENARI)
        return;
    finto_lavora(f, 0, fai_meteo_scenario, GINT_TO_POINTER(scenario), NULL);
}
void ph_finto_meteo_nuovi_dati(PhFinto *f) { finto_lavora(f, 0, fai_meteo_nuovi, NULL, NULL); }
void ph_finto_meteo_lento(PhFinto *f, bool si) { finto_lavora(f, 0, fai_meteo_lento, GINT_TO_POINTER(si), NULL); }
void ph_finto_meteo_intervallo(PhFinto *f, int secondi)
{
    finto_lavora(f, 0, fai_meteo_intervallo, GINT_TO_POINTER(MAX(secondi, 0)), NULL);
}

/* ---- Meteo: citta' a mano e ricerca dei luoghi ------------------------------- */
static void fai_meteo_localita(PhFinto *f, void *dati)
{
    g_free(f->meteo_citta);
    f->meteo_citta = dati != NULL ? g_memdup2(dati, sizeof(PhMeteoLuogo)) : NULL; /* copia propria */
    meteo_cambiato(f);
}

static void finto_meteo_localita(void *b, const PhMeteoLuogo *l)
{
    /* La copia e' liberata da finto_lavora anche se il lavoro non gira mai (chiusura prima). */
    PhMeteoLuogo *copia = NULL;
    if (l != NULL && (copia = malloc(sizeof *copia)) != NULL) /* malloc (non GLib): una perdita si vede con LeakSanitizer */
        memcpy(copia, l, sizeof *copia);
    finto_lavora(b, 0, fai_meteo_localita, copia, free);
}

/* Luoghi inventati (nomi e numeri di fantasia: nessun luogo vero). Sono piu' di 10
 * apposta: chi cerca «finto» deve vederne al massimo PH_METEO_LUOGHI_MAX. */
static const struct {
    const char *nome, *dettaglio;
    int32_t lat, lon;
} LUOGHI[] = {
    {"Sample City", "Sample Region, Sample Country", 1010, 2020},
    {"Upper Sample City", "Sample Region, Sample Country", 1110, 2120},
    {"Sample Town", "Other Region, Sample Country", -1500, 3010},
    {"Sample Town by the Sea", "Other Region, Sample Country", -1520, 3050},
    {"Sample Village", "Third Region, Sample Country", 2230, -4410},
    {"Sample Hamlet", "Third Region, Sample Country", 2260, -4450},
    {"Sample Castle", "Sample Region, Sample Country", 1090, 2090},
    {"Sample Harbor", "Other Region, Sample Country", -1480, 3070},
    {"Sample Hill", "Sample Region, Sample Country", 1070, 2150},
    {"Sample Valley", "Sample Region, Sample Country", 1130, 2050},
    {"Sample Island", "Sample Sea", 0, 0},
    {"Sample Lighthouse", "Sample Sea", 50, 60},
    /* Con la lettera accentata: la ricerca non deve badare alle maiuscole nemmeno su «É». */
    {"Café Central", "Sample Region, Sample Country", 1210, 2220},
};

/* Le ricerche in corso si annotano (thread GTK) e si tolgono PRIMA di chiuderle: finche' sono
 * nell'elenco il lavoro e' vivo, e ph_finto_meteo_ricerche_non_annullate puo' guardarlo. */
static void ricerca_togli(PhFinto *f, PhSrvLavoro *l)
{
    g_mutex_lock(&f->m_ric);
    g_ptr_array_remove_fast(f->ricerche, l);
    g_mutex_unlock(&f->m_ric);
}

typedef struct {
    Richiesta *r;
    PhFinto *f;
} RicercaCtx;

static void fai_meteo_cerca(PhFinto *f, void *dati)
{
    RicercaCtx *ctx = dati;
    Richiesta *r = ctx->r;
    ricerca_togli(f, r->l);
    r->finito = true;
    if (ph_servizi_lavoro_annullato(r->l)) {
        ph_servizi_lavoro_fatto(r->l, PH_SRV_ESITO_ERRORE, "annullato"); /* chiude senza avvisare */
        return;
    }
    if (f->meteo_ricerca_guasta) {
        ph_servizi_lavoro_fatto(r->l, PH_SRV_ESITO_ERRORE, "Il servizio meteo non risponde.");
        return;
    }
    PhMeteoLuogo v[PH_METEO_LUOGHI_MAX];
    size_t n = 0;
    char *cerca = g_utf8_casefold(r->a, -1);
    for (size_t i = 0; i < G_N_ELEMENTS(LUOGHI) && n < G_N_ELEMENTS(v); i++) {
        char *nome = g_utf8_casefold(LUOGHI[i].nome, -1);
        if (strstr(nome, cerca) != NULL) {
            memset(&v[n], 0, sizeof v[n]);
            g_strlcpy(v[n].nome, LUOGHI[i].nome, sizeof v[n].nome);
            g_strlcpy(v[n].dettaglio, LUOGHI[i].dettaglio, sizeof v[n].dettaglio);
            v[n].lat_centesimi = LUOGHI[i].lat;
            v[n].lon_centesimi = LUOGHI[i].lon;
            n++;
        }
        g_free(nome);
    }
    g_free(cerca);
    ph_servizi_lavoro_luoghi(r->l, PH_SRV_ESITO_OK, NULL, v, n);
}

/* Il lavoro non e' mai girato (chiusura prima): si toglie dall'elenco e lo si chiude. */
static void ricerca_libera(gpointer p)
{
    RicercaCtx *ctx = p;
    if (!ctx->r->finito)
        ricerca_togli(ctx->f, ctx->r->l);
    richiesta_libera(ctx->r);
    g_free(ctx);
}

static void finto_meteo_cerca(void *b, PhSrvLavoro *l, const char *testo, const char *lingua)
{
    PhFinto *f = b;
    RicercaCtx *ctx = g_new(RicercaCtx, 1);
    ctx->r = richiesta(l, testo, lingua);
    ctx->f = f;
    g_mutex_lock(&f->m_ric);
    g_ptr_array_add(f->ricerche, l);
    g_mutex_unlock(&f->m_ric);
    finto_lavora(f, atomic_load(&f->meteo_ricerca_lenta) ? 10000 : 300, fai_meteo_cerca, ctx, ricerca_libera);
}

int ph_finto_meteo_ricerche_non_annullate(PhFinto *f)
{
    int n = 0;
    g_mutex_lock(&f->m_ric);
    for (guint i = 0; i < f->ricerche->len; i++)
        if (!ph_servizi_lavoro_annullato(g_ptr_array_index(f->ricerche, i)))
            n++;
    g_mutex_unlock(&f->m_ric);
    return n;
}

static void fai_ricerca_guasta(PhFinto *f, void *dati) { f->meteo_ricerca_guasta = GPOINTER_TO_INT(dati) != 0; }

void ph_finto_meteo_ricerca_errore(PhFinto *f, bool si)
{
    finto_lavora(f, 0, fai_ricerca_guasta, GINT_TO_POINTER(si), NULL);
}
void ph_finto_meteo_ricerca_lenta(PhFinto *f, bool si) { atomic_store(&f->meteo_ricerca_lenta, si); }

/* Comandi (thread GTK -> thread di lavoro). */
static void fai_risposta_pannello(PhFinto *f, void *dati) { f->risposta_pannello = (PhSrvPannelloEsito)GPOINTER_TO_INT(dati); }
static void fai_inst_rifiutate(PhFinto *f, void *dati) { f->inst_rifiutate = GPOINTER_TO_INT(dati) != 0; }
static void fai_cal_illeggibile(PhFinto *f, void *dati) { f->cal_illeggibile = GPOINTER_TO_INT(dati) != 0; }
static void fai_prima_vuota(PhFinto *f, void *dati) { f->prima_vuota = GPOINTER_TO_INT(dati) != 0; }
static void fai_reg_guasta(PhFinto *f, void *dati) { f->reg_guasta = GPOINTER_TO_INT(dati) != 0; }

static void fai_schermo_spento(PhFinto *f, void *dati)
{
    f->schermo_spento = GPOINTER_TO_INT(dati) != 0;
    fprintf(stderr, "finto: schermo del telefono %s dal sistema\n", f->schermo_spento ? "spento" : "riacceso");
    finto_flussi_fermi(f, f->schermo_spento);
    pubblica_collegamento(f);
}

void ph_finto_pannello_risposta(PhFinto *f, PhSrvPannelloEsito esito)
{
    finto_lavora(f, 0, fai_risposta_pannello, GINT_TO_POINTER((int)esito), NULL);
}
void ph_finto_installazioni_rifiutate(PhFinto *f, bool si) { finto_lavora(f, 0, fai_inst_rifiutate, GINT_TO_POINTER(si), NULL); }
void ph_finto_calendario_illeggibile(PhFinto *f, bool si) { finto_lavora(f, 0, fai_cal_illeggibile, GINT_TO_POINTER(si), NULL); }
void ph_finto_prima_lettura_vuota(PhFinto *f, bool si) { finto_lavora(f, 0, fai_prima_vuota, GINT_TO_POINTER(si), NULL); }
void ph_finto_schermo_spento(PhFinto *f, bool si) { finto_lavora(f, 0, fai_schermo_spento, GINT_TO_POINTER(si), NULL); }
void ph_finto_registrazione_guasta(PhFinto *f, bool si) { finto_lavora(f, 0, fai_reg_guasta, GINT_TO_POINTER(si), NULL); }
unsigned ph_finto_chiavi_chieste(PhFinto *f) { return atomic_load(&f->chiavi); }
int ph_finto_ultima_densita(PhFinto *f) { return atomic_load(&f->ultima_densita); }
int ph_finto_ultima_pagina(PhFinto *f) { return atomic_load(&f->ultima_pagina); }
unsigned ph_finto_pagine_apertura(PhFinto *f) { return atomic_load(&f->pagine_apertura); }
unsigned ph_finto_pagine_inviate(PhFinto *f) { return atomic_load(&f->pagine_inviate); }
void ph_finto_pagina_rifiuta(PhFinto *f, bool si) { atomic_store(&f->pagina_rifiuta, si); }
unsigned ph_finto_pizzichi(PhFinto *f) { return atomic_load(&f->pizzichi); }

/* ---- Prima connessione: abbinamento (23 nov) --------------------------------- */
typedef struct {
    PhSrvLavoro *l;
    bool finito;
    char codice[7];
    bool manuale;
} Abbina;

static void abbina_libera(gpointer p)
{
    Abbina *a = p;
    if (!a->finito)
        ph_servizi_lavoro_abbina(a->l, PH_SRV_ABB_FALLITO, "Abbinamento interrotto.");
    explicit_bzero(a->codice, sizeof a->codice);
    g_free(a);
}

static void fai_abbina(PhFinto *f, void *dati)
{
    Abbina *a = dati;
    a->finito = true;
    if (f->stato != PH_SRV_NON_ABBINATO) {
        ph_servizi_lavoro_abbina(a->l, PH_SRV_ABB_RIFIUTATA, "Non c'e' nessun telefono da abbinare ora.");
        return;
    }
    /* Mai il codice nei log: solo l'esito. */
    fprintf(stderr, "finto: abbinamento%s\n", a->manuale ? " con indirizzo manuale" : "");
    if (strcmp(a->codice, "123456") == 0) {
        f->stato = PH_SRV_CERCO;
        PhSrvCollegamento c = {0};
        c.stato = PH_SRV_CERCO;
        g_strlcpy(c.motivo, "Abbinamento riuscito", sizeof c.motivo);
        c.telefoni_noti = f->noti;
        c.telefoni_trovati = 1;
        ph_servizi_pubblica_collegamento(f->s, &c);
        ph_servizi_lavoro_abbina(a->l, PH_SRV_ABB_RIUSCITO, NULL);
        f->noti++;
        f->raggiungibile = true;
        finto_lavora(f, 300, collegati, NULL, NULL);
        return;
    }
    const char *frase = strcmp(a->codice, "000000") == 0
                            ? "Apri sul telefono la schermata «Associa dispositivo con codice» e riprova."
                            : "Codice sbagliato o scaduto: riprova con il codice che mostra il telefono.";
    PhSrvCollegamento c = {0};
    c.stato = PH_SRV_NON_ABBINATO;
    g_strlcpy(c.motivo, frase, sizeof c.motivo);
    c.telefoni_noti = f->noti;
    c.telefoni_trovati = 1;
    ph_servizi_pubblica_collegamento(f->s, &c);
    ph_servizi_lavoro_abbina(a->l, PH_SRV_ABB_FALLITO, frase);
}

static void finto_abbina(void *b, PhSrvLavoro *l, const char *codice, const char *host, int porta)
{
    (void)porta;
    Abbina *a = g_new0(Abbina, 1);
    a->l = l;
    memcpy(a->codice, codice, 7);
    a->manuale = host != NULL;
    finto_lavora(b, 600, fai_abbina, a, abbina_libera);
}

static void fai_raggiungibile(PhFinto *f, void *dati)
{
    f->raggiungibile = GPOINTER_TO_INT(dati) != 0;
    fprintf(stderr, "finto: telefono %s in rete\n", f->raggiungibile ? "visibile" : "non visibile");
    if (f->stato == PH_SRV_CERCO)
        cerca(f);
}

void ph_finto_telefono_raggiungibile(PhFinto *f, bool si)
{
    finto_lavora(f, 0, fai_raggiungibile, GINT_TO_POINTER(si), NULL);
}

static const PhServiziBackend TABELLA = {
    .riconnetti_ora = finto_riconnetti,
    .pannello = finto_pannello,
    .volume = finto_volume,
    .muto = finto_muto,
    .appunti_imposta = finto_appunti,
    .icona_chiedi = finto_icona,
    .tel_elenca = finto_elenca,
    .tel_cerca = finto_cerca,
    .tel_crea_cartella = finto_crea,
    .tel_miniatura = finto_miniatura,
    .tel_leggi = finto_leggi,
    .tel_memoria = finto_memoria,
    .tr_accoda = finto_tr_accoda,
    .tr_annulla = finto_tr_annulla,
    .tr_annulla_tutto = finto_tr_annulla_tutto,
    .tr_rimuovi_finiti = finto_tr_rimuovi_finiti,
    .profilo_salva = finto_profilo_salva,
    .preferenze_salva = finto_preferenze_salva,
    .flusso_apri = finto_flusso_apri,
    .flusso_leggi = finto_flusso_leggi,
    .flusso_interrompi = finto_flusso_interrompi,
    .flusso_memoria = finto_flusso_memoria,
    .flusso_pausa = finto_flusso_pausa,
    .flusso_ridimensiona = finto_flusso_ridimensiona,
    .flusso_input = finto_flusso_input,
    .flusso_chiudi = finto_flusso_chiudi,
    .chiudi = finto_chiudi,
    .apk_installa = finto_apk_installa,
    .app_disinstalla = finto_app_disinstalla,
    .tr_annulla_verso = finto_tr_annulla_verso,
    .pannello_chiedi = finto_pannello_chiedi,
    .tel_memoria_dettaglio = finto_dettaglio,
    .tel_calendario = finto_calendario,
    .flusso_screenshot = finto_flusso_screenshot,
    .flusso_registra = finto_flusso_registra,
    .flusso_chiave = finto_flusso_chiave,
    .flusso_pagina = finto_flusso_pagina,
    .flusso_ridimensiona_densita = finto_flusso_ridimensiona_densita,
    .abbina = finto_abbina,
    .flusso_pizzico = finto_flusso_pizzico,
    .tel_calendario_intervallo = finto_calendario_intervallo,
    .tel_sveglia = finto_sveglia,
    .tel_batteria = finto_batteria,
    .controlli_osserva = finto_controlli_osserva,
    .controllo_imposta = finto_controllo_imposta,
    .trova_avvia = finto_trova_avvia,
    .trova_ferma = finto_trova_ferma,
    .meteo_osserva = finto_meteo_osserva,
    .meteo_localita = finto_meteo_localita,
    .meteo_cerca = finto_meteo_cerca,
};

PhFinto *ph_finto_nuovo(const PhFintoOpzioni *opzioni)
{
    PhFinto *f = g_new0(PhFinto, 1);
    if (opzioni != NULL)
        f->o = *opzioni;
    g_mutex_init(&f->m);
    f->flussi = g_ptr_array_new();
    f->batteria = 78;
    f->volume = 100;
    f->notifiche = g_ptr_array_new_with_free_func(notifica_libera);
    for (size_t i = G_N_ELEMENTS(NOTIFICHE_INIZIALI); i > 0; i--)
        aggiungi_notifica(f, &NOTIFICHE_INIZIALI[i - 1]);
    f->tr = g_ptr_array_new_with_free_func(tr_libera);
    f->disinstallate = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    atomic_init(&f->chiavi, 0);
    atomic_init(&f->pagine_apertura, 0);
    atomic_init(&f->pagine_inviate, 0);
    atomic_init(&f->ultima_pagina, 0);
    atomic_init(&f->pagina_rifiuta, false);
    atomic_init(&f->meteo_ricerca_lenta, false);
    g_mutex_init(&f->m_ric);
    f->ricerche = g_ptr_array_new();
    atomic_init(&f->ultima_densita, 0);
    atomic_init(&f->pizzichi, 0);
    costruisci_albero(f);
    f->s = ph_servizi_nuovi(&TABELLA, f);
    if (f->s == NULL) {
        g_ptr_array_unref(f->notifiche);
        g_ptr_array_unref(f->tr);
        g_hash_table_destroy(f->albero);
        g_ptr_array_unref(f->flussi);
        g_hash_table_destroy(f->disinstallate);
        g_ptr_array_unref(f->ricerche);
        g_mutex_clear(&f->m_ric);
        g_free(f);
        return NULL;
    }
    f->ctx = g_main_context_new();
    f->loop = g_main_loop_new(f->ctx, FALSE);

    GSource *s = g_timeout_source_new_seconds(60);
    g_source_set_callback(s, ogni_minuto, f, NULL);
    g_source_attach(s, f->ctx);
    g_source_unref(s);
    s = g_timeout_source_new(300);
    g_source_set_callback(s, ogni_300ms, f, NULL);
    g_source_attach(s, f->ctx);
    g_source_unref(s);
    if (f->o.notifiche_ogni_s >= 0) {
        s = g_timeout_source_new_seconds((guint)(f->o.notifiche_ogni_s > 0 ? f->o.notifiche_ogni_s : 20));
        g_source_set_callback(s, ogni_notifica, f, NULL);
        g_source_attach(s, f->ctx);
        g_source_unref(s);
    }
    finto_lavora(f, 0, avvia, NULL, NULL);
    f->thread = g_thread_new("ph-finto", thread_lavoro, f);
    return f;
}

PhServizi *ph_finto_servizi(PhFinto *f) { return f->s; }

void ph_finto_libera(PhFinto *f)
{
    if (f != NULL)
        ph_servizi_libera(f->s); /* chiama finto_chiudi, che libera f */
}

void ph_finto_blocca(PhFinto *f, bool bloccato) { finto_lavora(f, 0, fai_blocco, GINT_TO_POINTER(bloccato), NULL); }
void ph_finto_perdi(PhFinto *f, int secondi) { finto_lavora(f, 0, fai_perdita, GINT_TO_POINTER(secondi), NULL); }
void ph_finto_notifica_nuova(PhFinto *f) { finto_lavora(f, 0, fai_notifica, NULL, NULL); }
void ph_finto_notifica_aggiorna(PhFinto *f) { finto_lavora(f, 0, fai_aggiorna_notifica, NULL, NULL); }
void ph_finto_notifiche_illeggibili(PhFinto *f, bool si) { finto_lavora(f, 0, fai_illeggibili, GINT_TO_POINTER(si), NULL); }
void ph_finto_batteria(PhFinto *f, int percento) { finto_lavora(f, 0, fai_batteria, GINT_TO_POINTER(percento), NULL); }

static void fai_chiudi_app(PhFinto *f, void *dati)
{
    (void)dati;
    finto_flusso_ultimo_si_chiude(f);
}

void ph_finto_chiudi_app_da_sola(PhFinto *f) { finto_lavora(f, 0, fai_chiudi_app, NULL, NULL); }

static void fai_appunti(PhFinto *f, void *dati)
{
    ph_servizi_pubblica_appunti(f->s, dati);
}

void ph_finto_appunti_dal_telefono(PhFinto *f, const char *testo)
{
    finto_lavora(f, 0, fai_appunti, g_strdup(testo != NULL ? testo : "Text from the demo phone"), g_free);
}

/* ---- Immagini generate ------------------------------------------------------ */
static cairo_status_t scrivi_png(void *chiusura, const unsigned char *dati, unsigned int n)
{
    g_byte_array_append(chiusura, dati, n);
    return CAIRO_STATUS_SUCCESS;
}

static GBytes *png_da(cairo_surface_t *s)
{
    GByteArray *a = g_byte_array_new();
    if (cairo_surface_write_to_png_stream(s, scrivi_png, a) != CAIRO_STATUS_SUCCESS) {
        g_byte_array_unref(a);
        return NULL;
    }
    return g_byte_array_free_to_bytes(a);
}

static void colore(cairo_t *c, uint32_t rgb)
{
    cairo_set_source_rgb(c, ((rgb >> 16) & 0xff) / 255.0, ((rgb >> 8) & 0xff) / 255.0, (rgb & 0xff) / 255.0);
}

GBytes *finto_png_icona(const char *mono, uint32_t rgb, int lato)
{
    if (lato < 16 || lato > 512)
        lato = 128;
    cairo_surface_t *s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, lato, lato);
    cairo_t *c = cairo_create(s);
    double r = lato * 0.25, l = lato;
    cairo_new_sub_path(c);
    cairo_arc(c, l - r, r, r, -G_PI / 2, 0);
    cairo_arc(c, l - r, l - r, r, 0, G_PI / 2);
    cairo_arc(c, r, l - r, r, G_PI / 2, G_PI);
    cairo_arc(c, r, r, r, G_PI, 3 * G_PI / 2);
    cairo_close_path(c);
    colore(c, rgb);
    cairo_fill(c);
    cairo_select_font_face(c, "sans-serif", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(c, l * 0.36);
    cairo_text_extents_t e;
    cairo_text_extents(c, mono, &e);
    cairo_move_to(c, (l - e.width) / 2 - e.x_bearing, (l - e.height) / 2 - e.y_bearing);
    cairo_set_source_rgb(c, 1, 1, 1);
    cairo_show_text(c, mono);
    cairo_destroy(c);
    GBytes *b = png_da(s);
    cairo_surface_destroy(s);
    return b;
}

GBytes *finto_png_sfumatura(uint32_t da, uint32_t a, int larghezza, int altezza)
{
    cairo_surface_t *s = cairo_image_surface_create(CAIRO_FORMAT_RGB24, larghezza, altezza);
    cairo_t *c = cairo_create(s);
    cairo_pattern_t *p = cairo_pattern_create_linear(0, 0, larghezza * 0.3, altezza);
    cairo_pattern_add_color_stop_rgb(p, 0, ((da >> 16) & 0xff) / 255.0, ((da >> 8) & 0xff) / 255.0,
                                     (da & 0xff) / 255.0);
    cairo_pattern_add_color_stop_rgb(p, 1, ((a >> 16) & 0xff) / 255.0, ((a >> 8) & 0xff) / 255.0,
                                     (a & 0xff) / 255.0);
    cairo_set_source(c, p);
    cairo_paint(c);
    cairo_pattern_destroy(p);
    cairo_destroy(c);
    GBytes *b = png_da(s);
    cairo_surface_destroy(s);
    return b;
}
