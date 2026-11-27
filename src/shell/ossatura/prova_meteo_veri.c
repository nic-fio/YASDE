/* Prova del METEO dei servizi veri (sv_meteo.c) sopra pezzi sotto finti
 * (finti_sotto.c: ph_dati_posizione e meteo.h finti). Nessuna rete, nessun
 * telefono. Controlla: nessun collegamento finche' il meteo non e' osservato;
 * la precedenza (posizione del telefono sempre prima, la citta' scelta solo
 * senza posizione); ogni stato pubblicato; la lettura in piu' dopo
 * ricerca_avviata (una sola); la ripetizione e lo spegnimento; la ricerca dei
 * luoghi (risposta, errore, annullamento, sostituzione, spegnimento); la
 * chiusura con richieste in corso; niente coordinate ne' nomi nei log.
 * Nessun dormi(N): si aspetta un segnale con una scadenza (ASPETTA). Le
 * verifiche «non succede nulla» girano per un tempo breve DOPO un segnale
 * positivo che le ordina. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "config/lingua.h"
#include "finti_sotto.h"
#include "ossatura.h"
#include "shell/servizi_backend.h"
#include "shell/shell.h"
#include "sv_interno.h"

/* UNITA'. Il telefono da' la posizione in MILLESIMI di grado (multipli di 5, circa 500 m); la citta'
 * scelta a mano e' in CENTESIMI (PhMeteoLuogo) e arriva a ph_meteo_chiedi moltiplicata per 10. */
/* Valori scelti perche' un passaggio sbagliato di unita' si veda: la citta' A in centesimi (4546, 919)
 * diventa 45460 e 9190 in millesimi; il telefono manda 41895 e 12495 (multipli di 5) e arrivano identici.
 * (Nomi dei luoghi di fantasia: i numeri non corrispondono ai luoghi.) */
#define MILANO_LAT_MILL 41895
#define MILANO_LON_MILL 12495
#define ROMA_LAT_CENT 4546
#define ROMA_LON_CENT 919
#define ROMA_LAT_MILL (ROMA_LAT_CENT * 10)
#define ROMA_LON_MILL (ROMA_LON_CENT * 10)
#define NAPOLI_LAT_CENT 4085
#define NAPOLI_LON_CENT 1425
#define NAPOLI_LAT_MILL (NAPOLI_LAT_CENT * 10)
#define NAPOLI_LON_MILL (NAPOLI_LON_CENT * 10)

static FILE *err_orig; /* lo stderr vero: quello della prova e' catturato per controllare i log */
static int errori;
#define CONTROLLA(c)                                                                               \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            fprintf(err_orig, "prova_meteo_veri: fallito: %s (riga %d)\n", #c, __LINE__);         \
            errori++;                                                                              \
        }                                                                                          \
    } while (0)

#define ASPETTA(cond, ms)                                                                          \
    do {                                                                                           \
        gint64 _fine = g_get_monotonic_time() + (gint64)(ms)*1000;                                 \
        while (!(cond) && g_get_monotonic_time() < _fine) {                                        \
            while (g_main_context_iteration(NULL, FALSE)) {}                                       \
            g_usleep(1000);                                                                        \
        }                                                                                          \
    } while (0)

/* Verifica negativa: per `ms` non deve capitare che `cond` diventi falsa. */
#define RESTA(cond, ms)                                                                            \
    do {                                                                                           \
        gint64 _fine = g_get_monotonic_time() + (gint64)(ms)*1000;                                 \
        while (g_get_monotonic_time() < _fine) {                                                   \
            while (g_main_context_iteration(NULL, FALSE)) {}                                       \
            if (!(cond)) {                                                                         \
                CONTROLLA(cond);                                                                   \
                break;                                                                             \
            }                                                                                      \
            g_usleep(1000);                                                                        \
        }                                                                                          \
    } while (0)

/* ---- Il collegamento finto avvisa da un thread suo ---------------------------------- */
typedef struct {
    int tipo; /* 0 avviso di stato, 1 in perdita */
    PhColStato stato;
} Fuoco;

static gpointer fuoco_corpo(gpointer p)
{
    Fuoco *f = p;
    if (f->tipo == 1) {
        finti.opz.in_perdita(finti.opz.utente);
    } else {
        PhColAvviso a;
        memset(&a, 0, sizeof a);
        a.stato = f->stato;
        a.ha_telefono = f->stato == PH_COL_COLLEGATO || f->stato == PH_COL_BLOCCATO;
        snprintf(a.motivo, sizeof a.motivo, "frase");
        snprintf(a.telefono.nome, sizeof a.telefono.nome, "Modello finto");
        finti.opz.avviso(&a, finti.opz.utente);
    }
    return NULL;
}

static void fuoco(int tipo, PhColStato stato)
{
    Fuoco f = {tipo, stato};
    GThread *t = g_thread_new("fuoco", fuoco_corpo, &f);
    g_thread_join(t);
}

static void telefono_arriva(void)
{
    finti.connesso = true;
    fuoco(0, PH_COL_CERCO);
    fuoco(0, PH_COL_COLLEGATO);
}

static void telefono_parte(void)
{
    finti.connesso = false;
    fuoco(1, PH_COL_PERSO);
    fuoco(0, PH_COL_PERSO);
}

/* ---- Cio' che la shell vede ----------------------------------------------------------- */
typedef struct {
    PhSrvMeteoStato stato;
    PhSrvMeteoFonte fonte;
    char frase[192];
    char loc[128];
    bool ha_dati;
    double temp;
    int64_t ricevuta;
} Pub;
static Pub pubs[256];
static int n_pubs;
static bool nel_gtk_sempre = true;

static void ev_meteo(PhServizi *s, PhSrvEvento ev, const char *arg, void *u)
{
    (void)ev; (void)arg; (void)u;
    nel_gtk_sempre = nel_gtk_sempre && ph_shell_nel_thread_gtk();
    const PhSrvMeteo *m = ph_srv_meteo(s);
    if (n_pubs >= 256)
        return;
    Pub *p = &pubs[n_pubs++];
    p->stato = m->stato;
    p->fonte = ph_srv_meteo_fonte(s);
    snprintf(p->frase, sizeof p->frase, "%s", m->frase);
    snprintf(p->loc, sizeof p->loc, "%s", m->localita);
    p->ha_dati = m->ha_dati;
    p->temp = m->dati.adesso.temperatura;
    p->ricevuta = m->dati.ricevuta;
}
static const Pub *ultima(void) { return &pubs[n_pubs > 0 ? n_pubs - 1 : 0]; }

/* Aspetta una pubblicazione nuova rispetto a `base` con quello stato. */
static bool aspetta_stato(int base, PhSrvMeteoStato stato)
{
    ASPETTA(n_pubs > base && ultima()->stato == stato, 4000);
    return n_pubs > base && ultima()->stato == stato;
}

/* ---- Ricerca dei luoghi ------------------------------------------------------------------ */
static int n_luoghi[4];
static PhSrvEsito es_luoghi[4];
static size_t nv_luoghi[4];
static char frase_luoghi[4][128];
static char nome0_luoghi[4][128];
static void cb_luoghi(PhServizi *s, PhSrvEsito es, const char *frase, const PhMeteoLuogo *v, size_t n, void *u)
{
    (void)s;
    nel_gtk_sempre = nel_gtk_sempre && ph_shell_nel_thread_gtk();
    int k = GPOINTER_TO_INT(u);
    n_luoghi[k]++;
    es_luoghi[k] = es;
    nv_luoghi[k] = v != NULL ? n : 0;
    snprintf(frase_luoghi[k], sizeof frase_luoghi[k], "%s", frase != NULL ? frase : "");
    snprintf(nome0_luoghi[k], sizeof nome0_luoghi[k], "%s", v != NULL && n > 0 ? v[0].nome : "");
}

static PhMeteoLuogo luogo(const char *nome, int lat, int lon)
{
    PhMeteoLuogo l;
    memset(&l, 0, sizeof l);
    snprintf(l.nome, sizeof l.nome, "%s", nome);
    snprintf(l.dettaglio, sizeof l.dettaglio, "Regione, Paese");
    l.lat_centesimi = lat;
    l.lon_centesimi = lon;
    return l;
}

/* Fa partire un giro con un cambio di citta' (a meteo osservato e senza posizione
 * del telefono) e aspetta che la posizione sia stata richiesta. */
static void giro_con_citta(PhServizi *s, const PhMeteoLuogo *l, int pos_prima)
{
    ph_srv_meteo_localita(s, l);
    ASPETTA(atomic_load(&finti.pos_n) > pos_prima, 4000);
}

int main(void)
{
    char *tmp = g_dir_make_tmp("yasde-prova-XXXXXX", NULL);
    CONTROLLA(tmp != NULL);
    g_setenv("XDG_CONFIG_HOME", tmp, TRUE);
    g_setenv("XDG_CACHE_HOME", tmp, TRUE);
    ph_lingua_imposta(PH_LINGUA_IT);
    ph_shell_segna_thread_gtk();

    /* Lo stderr della prova su un file: alla fine si controlla che coordinate e
     * nomi non ci siano. I fallimenti vanno comunque allo stderr vero. */
    int fd_vero = dup(2);
    err_orig = fdopen(fd_vero, "w");
    char *log_percorso = g_build_filename(tmp, "stderr.txt", NULL);
    CONTROLLA(freopen(log_percorso, "w", stderr) != NULL);

    PhCfgPreferenze pref;
    ph_cfg_preferenze_predefinite(&pref);
    PhErrore e = {{0}};
    finti.connesso = false; /* all'inizio nessun telefono */
    PhServizi *s = ph_ossatura_servizi_veri(&pref, false, &e);
    CONTROLLA(s != NULL);
    if (s == NULL)
        return 1;
    ph_srv_ascolta(s, PH_SRV_EV_METEO, ev_meteo, NULL);
    const PhMeteoLuogo roma = luogo("Roma", ROMA_LAT_CENT, ROMA_LON_CENT);
    const PhMeteoLuogo napoli = luogo("Napoli", NAPOLI_LAT_CENT, NAPOLI_LON_CENT);

    /* ---- 1. Nessun widget Meteo: nessun collegamento, nessuna posizione, nessun oggetto. */
    ph_srv_meteo_localita(s, NULL);
    RESTA(atomic_load(&finti.m_creati) == 0 && atomic_load(&finti.m_chiedi_n) == 0 && atomic_load(&finti.pos_n) == 0, 100);

    /* ---- 2. Osservato, nessun telefono, nessuna citta': NON_COLLEGATO, e nessuna richiesta in rete. */
    int b = n_pubs;
    /* Prima prova: il pezzo meteo non si crea -> NON_DISPONIBILE; e si riprova da solo al giro dopo (timer),
     * senza aspettare un'altra accensione. */
    atomic_store(&finti.m_nuovo_rifiuta, true);
    sv_prova_meteo_periodo_ms = 150;
    ph_srv_meteo_osserva(s, true);
    CONTROLLA(aspetta_stato(b, PH_SRV_METEO_NON_DISPONIBILE) && ultima()->frase[0] != '\0');
    CONTROLLA(atomic_load(&finti.m_creati) == 0 && atomic_load(&finti.pos_n) == 0);
    b = n_pubs;
    atomic_store(&finti.m_nuovo_rifiuta, false);
    ASPETTA(atomic_load(&finti.m_creati) == 1, 4000);
    CONTROLLA(atomic_load(&finti.m_creati) == 1);
    CONTROLLA(aspetta_stato(b, PH_SRV_METEO_NON_COLLEGATO));
    ph_srv_meteo_osserva(s, false);
    sv_prova_meteo_periodo_ms = 5 * 60 * 1000;
    CONTROLLA(aspetta_stato(b, PH_SRV_METEO_SPENTO));
    b = n_pubs;
    ph_srv_meteo_osserva(s, true);
    CONTROLLA(aspetta_stato(b, PH_SRV_METEO_NON_COLLEGATO));
    CONTROLLA(ultima()->frase[0] != '\0' && !ultima()->ha_dati && ultima()->fonte == PH_SRV_METEO_FONTE_NESSUNA);
    CONTROLLA(pubs[b].stato == PH_SRV_METEO_IN_ATTESA); /* la prima e' quella del pezzo comune */
    CONTROLLA(atomic_load(&finti.m_creati) == 1 && atomic_load(&finti.m_chiedi_n) == 0 && atomic_load(&finti.pos_n) == 0);

    /* ---- 3. Citta' scelta, nessun telefono: previsioni per la citta' (A_MANO). */
    b = n_pubs;
    ph_srv_meteo_localita(s, &roma);
    ASPETTA(atomic_load(&finti.m_chiedi_n) == 1, 4000);
    CONTROLLA(atomic_load(&finti.m_chiedi_n) == 1 && atomic_load(&finti.m_lat) == ROMA_LAT_MILL && atomic_load(&finti.m_lon) == ROMA_LON_MILL);
    CONTROLLA(atomic_load(&finti.m_lat) == 45460 && atomic_load(&finti.m_lon) == 9190); /* i valori esatti: centesimi x 10 */
    CONTROLLA(atomic_load(&finti.pos_n) == 0); /* nessun telefono: nessuna lettura */
    CONTROLLA(aspetta_stato(b, PH_SRV_METEO_IN_ATTESA)); /* da «nessun telefono» ad «in attesa» */
    PhMeteoPrevisione p21 = finti_previsione(21);
    b = n_pubs;
    CONTROLLA(finti_meteo_rispondi(&p21, NULL));
    CONTROLLA(aspetta_stato(b, PH_SRV_METEO_PRONTO));
    CONTROLLA(ultima()->fonte == PH_SRV_METEO_FONTE_A_MANO && strcmp(ultima()->loc, "Roma") == 0 && ultima()->ha_dati &&
              ultima()->temp == 21 && ultima()->frase[0] == '\0');

    /* ---- 4. Arriva il telefono con una posizione: vince sulla citta'. La richiesta di ricerca e' permessa la prima volta. */
    finti_posizione(PH_DATI_POS_NOTA, MILANO_LAT_MILL, MILANO_LON_MILL, "Milano", false);
    int pos0 = atomic_load(&finti.pos_n), ch0 = atomic_load(&finti.m_chiedi_n);
    telefono_arriva();
    ASPETTA(atomic_load(&finti.m_chiedi_n) == ch0 + 1, 8000);
    CONTROLLA(atomic_load(&finti.pos_n) == pos0 + 1 && atomic_load(&finti.pos_permessa_n) == 1);
    CONTROLLA(atomic_load(&finti.m_lat) == MILANO_LAT_MILL && atomic_load(&finti.m_lon) == MILANO_LON_MILL);
    PhMeteoPrevisione p18 = finti_previsione(18);
    b = n_pubs;
    CONTROLLA(finti_meteo_rispondi(&p18, NULL));
    CONTROLLA(aspetta_stato(b, PH_SRV_METEO_PRONTO));
    CONTROLLA(ultima()->fonte == PH_SRV_METEO_FONTE_TELEFONO && strcmp(ultima()->loc, "Milano") == 0 && ultima()->temp == 18);
    /* Con la posizione del telefono un cambio di citta' non fa nulla (nessuna lettura in piu'). */
    pos0 = atomic_load(&finti.pos_n);
    ph_srv_meteo_localita(s, &napoli);

    /* ---- 5. Telefono perso: si usa la citta' (Napoli, scelta poco fa); ritrovato con la posizione spenta, ancora la citta'. */
    ch0 = atomic_load(&finti.m_chiedi_n);
    telefono_parte();
    ASPETTA(atomic_load(&finti.m_chiedi_n) == ch0 + 1, 8000);
    CONTROLLA(atomic_load(&finti.pos_n) == pos0); /* il cambio di citta' non ha fatto leggere nulla; la perdita nemmeno (nessun telefono) */
    CONTROLLA(atomic_load(&finti.m_lat) == NAPOLI_LAT_MILL && atomic_load(&finti.m_lon) == NAPOLI_LON_MILL);
    b = n_pubs;
    CONTROLLA(finti_meteo_rispondi(&p21, NULL));
    CONTROLLA(aspetta_stato(b, PH_SRV_METEO_PRONTO));
    CONTROLLA(ultima()->fonte == PH_SRV_METEO_FONTE_A_MANO && strcmp(ultima()->loc, "Napoli") == 0);

    finti_posizione(PH_DATI_POS_SPENTA, 0, 0, NULL, false);
    ch0 = atomic_load(&finti.m_chiedi_n);
    telefono_arriva();
    ASPETTA(atomic_load(&finti.m_chiedi_n) == ch0 + 1, 8000);
    CONTROLLA(atomic_load(&finti.pos_n) == pos0 + 1 && atomic_load(&finti.pos_permessa_n) == 1 &&
              !atomic_load(&finti.pos_ultima_permessa)); /* il permesso vale una volta ogni 6 ore */
    CONTROLLA(atomic_load(&finti.m_lat) == NAPOLI_LAT_MILL);
    b = n_pubs;
    CONTROLLA(finti_meteo_rispondi(&p21, NULL));
    CONTROLLA(aspetta_stato(b, PH_SRV_METEO_PRONTO));
    CONTROLLA(ultima()->fonte == PH_SRV_METEO_FONTE_A_MANO);

    /* ---- 6. Posizione NEGATA, SCONOSCIUTA, errore (-1): ancora la citta' (cambiando citta' si rifa' il giro). */
    PhDatiPosEsito sequenza[] = {PH_DATI_POS_NEGATA, PH_DATI_POS_SCONOSCIUTA};
    for (int i = 0; i < 2; i++) {
        finti_posizione(sequenza[i], 0, 0, NULL, false);
        pos0 = atomic_load(&finti.pos_n);
        ch0 = atomic_load(&finti.m_chiedi_n);
        const PhMeteoLuogo *l = i == 0 ? &roma : &napoli;
        giro_con_citta(s, l, pos0);
        ASPETTA(atomic_load(&finti.m_chiedi_n) == ch0 + 1, 4000);
        CONTROLLA(atomic_load(&finti.m_chiedi_n) == ch0 + 1 && atomic_load(&finti.m_lat) == l->lat_centesimi * 10);
    }
    atomic_store(&finti.pos_rifiuta, true);
    pos0 = atomic_load(&finti.pos_n);
    ch0 = atomic_load(&finti.m_chiedi_n);
    giro_con_citta(s, &roma, pos0);
    ASPETTA(atomic_load(&finti.m_chiedi_n) == ch0 + 1, 4000);
    CONTROLLA(atomic_load(&finti.m_chiedi_n) == ch0 + 1 && atomic_load(&finti.m_lat) == ROMA_LAT_MILL);
    b = n_pubs;
    CONTROLLA(finti_meteo_rispondi(&p21, NULL));
    CONTROLLA(aspetta_stato(b, PH_SRV_METEO_PRONTO));
    CONTROLLA(ultima()->fonte == PH_SRV_METEO_FONTE_A_MANO && strcmp(ultima()->loc, "Roma") == 0);

    /* ---- 7. Senza citta': il perche' (spenta, negata, sconosciuta, errore), con i dati vecchi. */
    char frasi[4][192];
    struct { PhDatiPosEsito es; bool rifiuta; } casi[4] = {{PH_DATI_POS_SPENTA, false}, {PH_DATI_POS_NEGATA, false},
                                                           {PH_DATI_POS_SCONOSCIUTA, false}, {PH_DATI_POS_NOTA, true}};
    for (int i = 0; i < 4; i++) {
        finti_posizione(casi[i].es, 0, 0, NULL, false);
        atomic_store(&finti.pos_rifiuta, casi[i].rifiuta);
        pos0 = atomic_load(&finti.pos_n);
        ph_srv_meteo_localita(s, &napoli); /* una citta' diversa dall'ultima... */
        ASPETTA(atomic_load(&finti.pos_n) > pos0, 4000);
        pos0 = atomic_load(&finti.pos_n);
        b = n_pubs;
        ph_srv_meteo_localita(s, NULL); /* ...e via la citta': il giro si rifa' e non c'e' posizione */
        ASPETTA(atomic_load(&finti.pos_n) > pos0, 4000);
        ASPETTA(n_pubs > b && ultima()->stato == PH_SRV_METEO_SENZA_POSIZIONE, 4000);
        CONTROLLA(ultima()->stato == PH_SRV_METEO_SENZA_POSIZIONE && ultima()->frase[0] != '\0');
        CONTROLLA(ultima()->ha_dati && ultima()->temp == 21 && ultima()->fonte == PH_SRV_METEO_FONTE_A_MANO);
        snprintf(frasi[i], sizeof frasi[i], "%s", ultima()->frase);
    }
    CONTROLLA(strcmp(frasi[0], frasi[1]) != 0 && strcmp(frasi[0], frasi[2]) != 0 && strcmp(frasi[1], frasi[2]) != 0);
    CONTROLLA(strcmp(frasi[2], frasi[3]) == 0); /* errore = nessuna posizione nota */
    atomic_store(&finti.pos_rifiuta, false);
    finti_posizione(PH_DATI_POS_SPENTA, 0, 0, NULL, false);
    ch0 = atomic_load(&finti.m_chiedi_n);
    b = n_pubs; /* letto PRIMA: la pubblicazione IN_ATTESA di questa richiesta puo' arrivare dopo */
    ph_srv_meteo_localita(s, &roma);
    ASPETTA(atomic_load(&finti.m_chiedi_n) > ch0, 4000);
    CONTROLLA(aspetta_stato(b, PH_SRV_METEO_IN_ATTESA)); /* consegnata al ciclo GTK prima di proseguire */

    /* ---- 8. Citta' presente e telefono presente con la posizione: vince il telefono. */
    finti_posizione(PH_DATI_POS_NOTA, MILANO_LAT_MILL, MILANO_LON_MILL, "Milano", false);
    pos0 = atomic_load(&finti.pos_n);
    ch0 = atomic_load(&finti.m_chiedi_n);
    ph_srv_meteo_localita(s, &napoli);
    ASPETTA(atomic_load(&finti.m_chiedi_n) == ch0 + 1, 4000);
    CONTROLLA(atomic_load(&finti.pos_n) == pos0 + 1 && atomic_load(&finti.m_lat) == MILANO_LAT_MILL && atomic_load(&finti.m_lon) == MILANO_LON_MILL);

    /* ---- 8b. La richiesta del telefono (Milano, in volo) e' sostituita da quella della citta' (telefono perso):
     * la callback della VECCHIA, consegnata dopo la nuova, non pubblica nulla e non viene attribuita alla nuova. */
    ch0 = atomic_load(&finti.m_chiedi_n);
    /* Il passo 8 puo' avere ancora una pubblicazione in coda al ciclo GTK (la guarda solo m_chiedi_n):
     * si svuota la coda PRIMA di leggere b, come al passo 7. */
    while (g_main_context_iteration(NULL, FALSE)) {}
    b = n_pubs;
    telefono_parte();
    ASPETTA(atomic_load(&finti.m_chiedi_n) == ch0 + 1, 8000);
    CONTROLLA(atomic_load(&finti.m_lat) == NAPOLI_LAT_MILL); /* Napoli, la citta' in uso */
    CONTROLLA(finti_meteo_rispondi_vecchia(&p18, NULL)); /* arriva al contesto di Milano */
    RESTA(n_pubs == b, 100);
    CONTROLLA(finti_meteo_rispondi(&p21, NULL));
    CONTROLLA(aspetta_stato(b, PH_SRV_METEO_PRONTO));
    CONTROLLA(ultima()->fonte == PH_SRV_METEO_FONTE_A_MANO && strcmp(ultima()->loc, "Napoli") == 0 && ultima()->temp == 21);
    ch0 = atomic_load(&finti.m_chiedi_n);
    telefono_arriva(); /* il telefono torna (richiesta in volo, senza risposta) */
    ASPETTA(atomic_load(&finti.m_chiedi_n) == ch0 + 1, 8000);

    /* ---- 9. ricerca_avviata: UNA lettura in piu' dopo ~15 s (qui 150 ms), senza permesso nuovo, poi basta.
     * Il permesso e' qui sempre concesso (0 s) per far vedere che la rilettura NON lo chiede. */
    sv_prova_meteo_ricerca_ms = 150;
    sv_prova_meteo_permesso_s = 0;
    ph_srv_meteo_localita(s, NULL); /* con la posizione del telefono non fa rifare il giro */
    finti_posizione(PH_DATI_POS_SCONOSCIUTA, 0, 0, NULL, true); /* la risposta ha SEMPRE ricerca_avviata */
    pos0 = atomic_load(&finti.pos_n);
    int perm0 = atomic_load(&finti.pos_permessa_n);
    telefono_parte();
    telefono_arriva();
    ASPETTA(atomic_load(&finti.pos_n) >= pos0 + 2, 8000);
    CONTROLLA(atomic_load(&finti.pos_n) == pos0 + 2);
    CONTROLLA(atomic_load(&finti.pos_permessa_n) == perm0 + 1 && !atomic_load(&finti.pos_ultima_permessa));
    int fermo = atomic_load(&finti.pos_n);
    RESTA(atomic_load(&finti.pos_n) == fermo, 500); /* nessuna terza lettura */
    sv_prova_meteo_ricerca_ms = 15 * 1000;
    sv_prova_meteo_permesso_s = 6 * 3600;

    /* ---- 10. Ripetizione ogni periodo e spegnimento: nessun timer resta. */
    finti_posizione(PH_DATI_POS_NOTA, MILANO_LAT_MILL, MILANO_LON_MILL, "Milano", false);
    sv_prova_meteo_periodo_ms = 150;
    pos0 = atomic_load(&finti.pos_n);
    ch0 = atomic_load(&finti.m_chiedi_n);
    telefono_parte();
    telefono_arriva();
    ASPETTA(atomic_load(&finti.pos_n) >= pos0 + 4 && atomic_load(&finti.m_chiedi_n) >= ch0 + 4, 8000);
    CONTROLLA(atomic_load(&finti.m_chiedi_n) >= ch0 + 4); /* ph_meteo_chiedi a ogni giro: lo decide il pezzo meteo */
    int an0 = atomic_load(&finti.m_annulla_n);
    b = n_pubs;
    ph_srv_meteo_osserva(s, false);
    CONTROLLA(atomic_load(&finti.m_annulla_n) == an0 + 1); /* annulla subito, sul thread GTK */
    CONTROLLA(aspetta_stato(b, PH_SRV_METEO_SPENTO));
    sv_prova_meteo_periodo_ms = 5 * 60 * 1000;
    /* Poi tutto fermo (nessun timer resta). */
    fermo = atomic_load(&finti.pos_n);
    int fermo_ch = atomic_load(&finti.m_chiedi_n);
    RESTA(atomic_load(&finti.pos_n) == fermo && atomic_load(&finti.m_chiedi_n) == fermo_ch, 600);
    /* Un cambio di citta' o un telefono che arriva, da spento, non collegano nulla. */
    ph_srv_meteo_localita(s, &napoli);
    telefono_parte();
    telefono_arriva();
    RESTA(atomic_load(&finti.pos_n) == fermo && atomic_load(&finti.m_chiedi_n) == fermo_ch, 400);

    /* ---- 11. Spegnimento con un giro in corso (la posizione e' ferma al cancello): niente richiesta dopo. */
    finti_posizione(PH_DATI_POS_NOTA, MILANO_LAT_MILL, MILANO_LON_MILL, "Milano", false);
    atomic_store(&finti.pos_gate, true);
    ph_srv_meteo_osserva(s, true);
    ASPETTA(atomic_load(&finti.pos_in_corso), 4000);
    CONTROLLA(atomic_load(&finti.pos_in_corso));
    ph_srv_meteo_osserva(s, false);
    finti_apri_cancello_posizione();
    ASPETTA(!atomic_load(&finti.pos_in_corso), 4000);
    fermo_ch = atomic_load(&finti.m_chiedi_n);
    RESTA(atomic_load(&finti.m_chiedi_n) == fermo_ch, 400);
    /* Spento e riacceso nel mezzo: la richiesta e' una sola, del giro nuovo. */
    atomic_store(&finti.pos_gate, true);
    ph_srv_meteo_osserva(s, true);
    ASPETTA(atomic_load(&finti.pos_in_corso), 4000);
    ph_srv_meteo_osserva(s, false);
    ph_srv_meteo_osserva(s, true);
    finti_apri_cancello_posizione();
    ASPETTA(atomic_load(&finti.m_chiedi_n) >= fermo_ch + 1, 4000);
    RESTA(atomic_load(&finti.m_chiedi_n) == fermo_ch + 1, 400);
    b = n_pubs;
    CONTROLLA(finti_meteo_rispondi(&p18, NULL));
    CONTROLLA(aspetta_stato(b, PH_SRV_METEO_PRONTO));

    /* ---- 11b. Spegnimento TRA il controllo e ph_meteo_chiedi (il finto ferma chiedi all'ingresso): la richiesta
     * che parte dopo lo spegnimento viene annullata, nessuna resta viva, nessuna pubblicazione dopo SPENTO. */
    b = n_pubs;
    ph_srv_meteo_osserva(s, false);
    CONTROLLA(aspetta_stato(b, PH_SRV_METEO_SPENTO));
    atomic_store(&finti.m_chiedi_gate, true);
    an0 = atomic_load(&finti.m_annulla_n);
    ph_srv_meteo_osserva(s, true);
    ASPETTA(atomic_load(&finti.m_chiedi_in_corso), 4000);
    CONTROLLA(atomic_load(&finti.m_chiedi_in_corso));
    while (g_main_context_iteration(NULL, FALSE)) {}
    b = n_pubs;
    ph_srv_meteo_osserva(s, false);
    CONTROLLA(atomic_load(&finti.m_annulla_n) == an0 + 1);
    finti_apri_cancello_chiedi();
    ASPETTA(atomic_load(&finti.m_annulla_n) == an0 + 2, 4000); /* il giro annulla di nuovo la richiesta nata tardi */
    CONTROLLA(atomic_load(&finti.m_annulla_n) == an0 + 2);
    CONTROLLA(!finti_meteo_rispondi(&p18, NULL)); /* nessuna richiesta viva */
    CONTROLLA(aspetta_stato(b, PH_SRV_METEO_SPENTO));
    RESTA(n_pubs == b + 1, 150); /* solo SPENTO */

    /* ---- 12. Errore del servizio: NON_DISPONIBILE con i dati vecchi e la loro ora; anche se chiedi rifiuta. */
    ch0 = atomic_load(&finti.m_chiedi_n);
    ph_srv_meteo_osserva(s, false);
    ph_srv_meteo_osserva(s, true);
    ASPETTA(atomic_load(&finti.m_chiedi_n) == ch0 + 1, 4000);
    b = n_pubs;
    CONTROLLA(finti_meteo_rispondi(&p18, NULL));
    CONTROLLA(aspetta_stato(b, PH_SRV_METEO_PRONTO));
    atomic_store(&finti.m_chiedi_rifiuta, true);
    b = n_pubs;
    telefono_parte(); /* un solo giro: la citta' (Napoli) e ph_meteo_chiedi rifiutato */
    CONTROLLA(aspetta_stato(b, PH_SRV_METEO_NON_DISPONIBILE));
    CONTROLLA(ultima()->frase[0] != '\0' && ultima()->ha_dati && ultima()->temp == 18);
    atomic_store(&finti.m_chiedi_rifiuta, false);
    ch0 = atomic_load(&finti.m_chiedi_n);
    telefono_arriva();
    ASPETTA(atomic_load(&finti.m_chiedi_n) == ch0 + 1, 8000);
    b = n_pubs;
    CONTROLLA(finti_meteo_rispondi(&p21, NULL));
    CONTROLLA(aspetta_stato(b, PH_SRV_METEO_PRONTO));
    int64_t ricevuta = ultima()->ricevuta;
    /* Il servizio non risponde (frase del pezzo meteo), dati vecchi presenti. */
    ch0 = atomic_load(&finti.m_chiedi_n);
    telefono_parte();
    ASPETTA(atomic_load(&finti.m_chiedi_n) == ch0 + 1, 8000);
    b = n_pubs;
    CONTROLLA(finti_meteo_rispondi(NULL, "Servizio meteo non raggiungibile")); /* frase non riconosciuta: a schermo la generica */
    CONTROLLA(aspetta_stato(b, PH_SRV_METEO_NON_DISPONIBILE));
    CONTROLLA(strcmp(ultima()->frase, "Previsioni del tempo non disponibili in questo momento.") == 0 && ultima()->ha_dati &&
              ultima()->ricevuta == ricevuta && ultima()->temp == 21);
    /* Una risposta dopo lo spegnimento non arriva (annulla), e non pubblica altro che SPENTO. */
    b = n_pubs;
    ph_srv_meteo_osserva(s, false);
    CONTROLLA(!finti_meteo_rispondi(&p21, NULL));
    RESTA(n_pubs <= b + 1, 100);
    CONTROLLA(atomic_load(&finti.m_creati) == 1); /* un solo oggetto per tutto il tempo, spenti e riaccesi */

    /* ---- 12b. Accendendo di nuovo da «nessun telefono» «in attesa» esce UNA volta sola; da spento: nessun
     * timer vivo, dati e localita' azzerati in memoria, contesti vecchi liberati. */
    ph_srv_meteo_localita(s, NULL);
    b = n_pubs;
    ph_srv_meteo_osserva(s, true);
    CONTROLLA(aspetta_stato(b, PH_SRV_METEO_NON_COLLEGATO));
    b = n_pubs;
    ph_srv_meteo_osserva(s, false);
    CONTROLLA(aspetta_stato(b, PH_SRV_METEO_SPENTO));
    ph_srv_meteo_localita(s, &roma); /* spento: nessun giro */
    ch0 = atomic_load(&finti.m_chiedi_n);
    b = n_pubs;
    ph_srv_meteo_osserva(s, true);
    ASPETTA(atomic_load(&finti.m_chiedi_n) == ch0 + 1, 4000);
    while (g_main_context_iteration(NULL, FALSE)) {}
    int in_attesa = 0;
    for (int i = b; i < n_pubs; i++)
        in_attesa += pubs[i].stato == PH_SRV_METEO_IN_ATTESA;
    CONTROLLA(in_attesa == 1);
    b = n_pubs;
    CONTROLLA(finti_meteo_rispondi(&p21, NULL));
    CONTROLLA(aspetta_stato(b, PH_SRV_METEO_PRONTO));
    ch0 = atomic_load(&finti.m_chiedi_n);
    ph_srv_meteo_localita(s, &napoli);
    ASPETTA(atomic_load(&finti.m_chiedi_n) == ch0 + 1, 4000);
    ph_srv_meteo_localita(s, &roma); /* la richiesta di Napoli, senza risposta, diventa «vecchia» */
    ASPETTA(atomic_load(&finti.m_chiedi_n) == ch0 + 2, 4000);
    ASPETTA(!sv_prova_meteo_in_giro(sv_prova_sv), 4000);
    CONTROLLA(sv_prova_meteo_vecchie(sv_prova_sv) == 1);
    CONTROLLA(!sv_prova_meteo_azzerato(sv_prova_sv)); /* i dati ci sono */
    CONTROLLA(atomic_load(&sv_prova_meteo_sorgenti) == 1); /* il timer del prossimo giro */
    b = n_pubs;
    ph_srv_meteo_osserva(s, false);
    CONTROLLA(aspetta_stato(b, PH_SRV_METEO_SPENTO));
    CONTROLLA(atomic_load(&sv_prova_meteo_sorgenti) == 0); /* nessun timer resta */
    CONTROLLA(sv_prova_meteo_vecchie(sv_prova_sv) == 0);
    CONTROLLA(sv_prova_meteo_azzerato(sv_prova_sv));

    /* ---- 13. Un altro telefono: dati, posizione e citta' del primo non si mostrano al secondo. */
    finti_posizione(PH_DATI_POS_NOTA, MILANO_LAT_MILL, MILANO_LON_MILL, "Milano", false);
    ph_srv_meteo_localita(s, &napoli);
    ch0 = atomic_load(&finti.m_chiedi_n);
    ph_srv_meteo_osserva(s, true); /* senza telefono: la citta' */
    ASPETTA(atomic_load(&finti.m_chiedi_n) == ch0 + 1, 8000);
    telefono_arriva(); /* e' ancora il telefono A: nessun azzeramento */
    ASPETTA(atomic_load(&finti.m_chiedi_n) == ch0 + 2, 8000);
    CONTROLLA(atomic_load(&finti.m_lat) == MILANO_LAT_MILL);
    b = n_pubs;
    CONTROLLA(finti_meteo_rispondi(&p18, NULL));
    CONTROLLA(aspetta_stato(b, PH_SRV_METEO_PRONTO));
    CONTROLLA(ultima()->fonte == PH_SRV_METEO_FONTE_TELEFONO && strcmp(ultima()->loc, "Milano") == 0);
    ch0 = atomic_load(&finti.m_chiedi_n);
    telefono_parte();
    ASPETTA(atomic_load(&finti.m_chiedi_n) == ch0 + 1, 8000); /* senza telefono: la citta' */
    int ch_b = atomic_load(&finti.m_chiedi_n);
    int perm_b = atomic_load(&finti.pos_permessa_n);
    atomic_store(&finti.altro_seriale, true);
    finti_posizione(PH_DATI_POS_SCONOSCIUTA, 0, 0, NULL, false);
    b = n_pubs;
    telefono_arriva();
    ASPETTA(n_pubs > b + 1 && ultima()->stato == PH_SRV_METEO_SENZA_POSIZIONE, 8000);
    CONTROLLA(n_pubs > b && ultima()->stato == PH_SRV_METEO_SENZA_POSIZIONE);
    CONTROLLA(!ultima()->ha_dati && ultima()->loc[0] == '\0' && ultima()->fonte == PH_SRV_METEO_FONTE_NESSUNA);
    bool vista_attesa = false;
    for (int i = b; i < n_pubs; i++) {
        if (pubs[i].stato == PH_SRV_METEO_IN_ATTESA)
            vista_attesa = pubs[i].loc[0] == '\0' && !pubs[i].ha_dati;
        CONTROLLA(pubs[i].loc[0] == '\0' && !pubs[i].ha_dati); /* mai la localita' ne' i dati di A */
    }
    CONTROLLA(vista_attesa);
    CONTROLLA(atomic_load(&finti.m_chiedi_n) == ch_b); /* la citta' di A non e' stata usata per B */
    CONTROLLA(atomic_load(&finti.pos_permessa_n) == perm_b + 1 && atomic_load(&finti.pos_ultima_permessa));
    /* Il desktop rimanda la citta' del profilo di B: allora si usa. */
    ph_srv_meteo_localita(s, &roma);
    ASPETTA(atomic_load(&finti.m_chiedi_n) == ch_b + 1, 4000);
    CONTROLLA(atomic_load(&finti.m_lat) == ROMA_LAT_MILL);
    ph_srv_meteo_osserva(s, false);

    /* ---- 14. Ricerca dei luoghi. */
    ch0 = atomic_load(&finti.m_chiedi_n);
    ph_srv_meteo_osserva(s, true);
    ASPETTA(atomic_load(&finti.m_chiedi_n) > ch0, 4000);
    ph_srv_meteo_cerca(s, "Roma", "it", NULL, cb_luoghi, GINT_TO_POINTER(0));
    ASPETTA(atomic_load(&finti.m_cerca_n) == 1, 4000);
    CONTROLLA(atomic_load(&finti.m_cerca_n) == 1 && strcmp(finti.m_cerca_testo, "Roma") == 0 && n_luoghi[0] == 0);
    PhMeteoLuogo due[2] = {luogo("Roma", ROMA_LAT_CENT, ROMA_LON_CENT), luogo("Roma Nord", 4560, 930)};
    CONTROLLA(finti_meteo_cerca_rispondi(due, 2, NULL));
    ASPETTA(n_luoghi[0] == 1, 4000);
    CONTROLLA(n_luoghi[0] == 1 && es_luoghi[0] == PH_SRV_ESITO_OK && nv_luoghi[0] == 2 && strcmp(nome0_luoghi[0], "Roma") == 0);
    /* Errore con frase (mai il testo cercato). */
    ph_srv_meteo_cerca(s, "Roma", "it", NULL, cb_luoghi, GINT_TO_POINTER(1));
    ASPETTA(atomic_load(&finti.m_cerca_n) == 2, 4000);
    CONTROLLA(finti_meteo_cerca_rispondi(NULL, 0, "troppe ricerche: riprova tra qualche minuto"));
    ASPETTA(n_luoghi[1] == 1, 4000);
    CONTROLLA(es_luoghi[1] == PH_SRV_ESITO_ERRORE && strcmp(frase_luoghi[1], "Troppe ricerche: riprova tra qualche minuto.") == 0);
    /* Nessun luogo trovato: OK con zero. */
    ph_srv_meteo_cerca(s, "Xx", "en", NULL, cb_luoghi, GINT_TO_POINTER(2));
    ASPETTA(atomic_load(&finti.m_cerca_n) == 3, 4000);
    CONTROLLA(finti_meteo_cerca_rispondi(due, 0, NULL));
    ASPETTA(n_luoghi[2] == 1, 4000);
    CONTROLLA(es_luoghi[2] == PH_SRV_ESITO_OK && nv_luoghi[2] == 0);
    /* Annullata da chi ha chiesto: la ricerca si ferma entro ~250 ms, la risposta non arriva. */
    GCancellable *ann = g_cancellable_new();
    ph_srv_meteo_cerca(s, "Roma", "it", ann, cb_luoghi, GINT_TO_POINTER(3));
    ASPETTA(atomic_load(&finti.m_cerca_n) == 4, 4000);
    int ca0 = atomic_load(&finti.m_cerca_annulla_n);
    gint64 t_ann = g_get_monotonic_time();
    g_cancellable_cancel(ann);
    ASPETTA(atomic_load(&finti.m_cerca_annulla_n) == ca0 + 1, 2000);
    CONTROLLA(atomic_load(&finti.m_cerca_annulla_n) == ca0 + 1);
    CONTROLLA(g_get_monotonic_time() - t_ann < 400 * 1000);
    CONTROLLA(!finti_meteo_cerca_rispondi(due, 2, NULL)); /* nessuno aspetta piu' */
    RESTA(n_luoghi[3] == 0, 100);
    g_object_unref(ann);
    /* Una ricerca ne sostituisce un'altra: la vecchia si chiude da sola (una volta), la nuova risponde. */
    memset(n_luoghi, 0, sizeof n_luoghi);
    ph_srv_meteo_cerca(s, "Roma", "it", NULL, cb_luoghi, GINT_TO_POINTER(0));
    ASPETTA(atomic_load(&finti.m_cerca_n) == 5, 4000);
    ph_srv_meteo_cerca(s, "Napoli", "it", NULL, cb_luoghi, GINT_TO_POINTER(1));
    ASPETTA(atomic_load(&finti.m_cerca_n) == 6 && n_luoghi[0] == 1, 4000);
    CONTROLLA(n_luoghi[0] == 1 && es_luoghi[0] == PH_SRV_ESITO_ERRORE && n_luoghi[1] == 0);
    /* La risposta della ricerca SOSTITUITA, consegnata dopo la nuova, non va alla nuova. */
    CONTROLLA(finti_meteo_cerca_rispondi_vecchia(due, 2, NULL));
    RESTA(n_luoghi[1] == 0 && n_luoghi[0] == 1, 100);
    CONTROLLA(finti_meteo_cerca_rispondi(due, 2, NULL));
    ASPETTA(n_luoghi[1] == 1, 4000);
    CONTROLLA(n_luoghi[0] == 1 && n_luoghi[1] == 1 && es_luoghi[1] == PH_SRV_ESITO_OK);
    /* ph_meteo_cerca che rifiuta: errore subito. */
    atomic_store(&finti.m_cerca_rifiuta, true);
    ph_srv_meteo_cerca(s, "Roma", "it", NULL, cb_luoghi, GINT_TO_POINTER(2));
    ASPETTA(n_luoghi[2] == 1, 4000);
    CONTROLLA(n_luoghi[2] == 1 && es_luoghi[2] == PH_SRV_ESITO_ERRORE);
    atomic_store(&finti.m_cerca_rifiuta, false);
    /* Spegnimento con la ricerca in corso: si chiude, ph_meteo_annulla la ferma. */
    memset(n_luoghi, 0, sizeof n_luoghi);
    ph_srv_meteo_cerca(s, "Roma", "it", NULL, cb_luoghi, GINT_TO_POINTER(3));
    ASPETTA(atomic_load(&finti.m_cerca_n) == 8, 4000);
    an0 = atomic_load(&finti.m_annulla_n);
    ph_srv_meteo_osserva(s, false);
    ASPETTA(n_luoghi[3] == 1, 4000);
    CONTROLLA(n_luoghi[3] == 1 && es_luoghi[3] == PH_SRV_ESITO_ERRORE && atomic_load(&finti.m_annulla_n) == an0 + 1);
    CONTROLLA(!finti_meteo_cerca_rispondi(due, 2, NULL));
    CONTROLLA(atomic_load(&sv_prova_meteo_sorgenti) == 0); /* ne' timer ne' sonda */

    /* ---- 16. Chiusura con una richiesta di previsioni e una ricerca in corso. */
    finti_posizione(PH_DATI_POS_NOTA, MILANO_LAT_MILL, MILANO_LON_MILL, "Milano", false);
    ch0 = atomic_load(&finti.m_chiedi_n);
    ph_srv_meteo_osserva(s, true);
    ASPETTA(atomic_load(&finti.m_chiedi_n) > ch0, 4000);
    ph_srv_meteo_cerca(s, "Roma", "it", NULL, cb_luoghi, GINT_TO_POINTER(0));
    ASPETTA(atomic_load(&finti.m_cerca_n) == 9, 4000);
    int ch_fin = atomic_load(&finti.m_chiedi_n);
    int n_l0 = n_luoghi[0];
    int pubs_prima = n_pubs;
    ph_servizi_libera(s);
    CONTROLLA(atomic_load(&sv_prova_meteo_sorgenti) == 0); /* nessun timer ne' sonda dopo la chiusura */
    CONTROLLA(atomic_load(&finti.m_chiusi) == 1 && atomic_load(&finti.m_creati) == 1);
    CONTROLLA(atomic_load(&finti.presti) == 0);
    CONTROLLA(!finti_meteo_rispondi(&p18, NULL) && !finti_meteo_cerca_rispondi(due, 2, NULL));
    while (g_main_context_iteration(NULL, FALSE)) {}
    CONTROLLA(atomic_load(&finti.m_chiedi_n) == ch_fin && atomic_load(&finti.m_chiedi_dopo_chiudi) == 0);
    (void)n_l0;
    (void)pubs_prima;
    CONTROLLA(nel_gtk_sempre);

    /* ---- Riservatezza: nessuna coordinata ne' nome nei log. */
    fflush(stderr);
    gchar *log = NULL;
    CONTROLLA(g_file_get_contents(log_percorso, &log, NULL, NULL));
    if (log != NULL) {
        fputs(log, err_orig);
        const char *vietate[] = {"41895", "12495", "45460", "9190", "40850", "14250", "4546", "4085", "1425", "Milano", "Roma", "Napoli", "Regione"};
        for (size_t i = 0; i < sizeof vietate / sizeof vietate[0]; i++)
            CONTROLLA(strstr(log, vietate[i]) == NULL);
        g_free(log);
    }
    char *cmd = g_strdup_printf("rm -rf '%s'", tmp);
    CONTROLLA(system(cmd) == 0);
    g_free(cmd);
    g_free(log_percorso);
    g_free(tmp);
    if (errori == 0)
        fprintf(err_orig, "prova_meteo_veri: tutto bene\n");
    return errori == 0 ? 0 : 1;
}
