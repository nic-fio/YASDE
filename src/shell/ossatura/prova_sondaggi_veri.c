/* Prova di chiamate recenti, rete e in riproduzione dei servizi veri
 * (sv_sondaggi.c) sopra pezzi sotto finti (finti_sotto.c). Nessun telefono.
 * Controlla: nessuna lettura finche' nessuno osserva; stati (ok, non disponibile,
 * permesso negato, lettura fallita); i valori cambiati si pubblicano; i comandi
 * al lettore (solo 1-3) e la rilettura subito dopo; niente letture dopo
 * osserva(false); nessuna sessione = «non disponibile» e ripartenza al ritorno;
 * chiusura con voci osservate; eventi solo sul thread GTK; niente nomi, numeri,
 * titoli o reti nei log. Nessun dormi(N): si aspetta un segnale con scadenza. */
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

static FILE *err_orig;
static int errori;
#define CONTROLLA(c)                                                                               \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            fprintf(err_orig, "prova_sondaggi_veri: fallito: %s (riga %d)\n", #c, __LINE__);      \
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

typedef struct {
    int tipo;
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

static PhServizi *S;
static int ev_n[3];
static bool nel_gtk_sempre = true;

static void ev(PhServizi *s, PhSrvEvento e, const char *arg, void *u)
{
    (void)s;
    (void)arg;
    (void)u;
    nel_gtk_sempre = nel_gtk_sempre && ph_shell_nel_thread_gtk();
    ev_n[e == PH_SRV_EV_CHIAMATE ? 0 : e == PH_SRV_EV_RETE ? 1 : 2]++;
}

static PhSrvLettura chi_l(void) { return ph_srv_chiamate(S)->stato; }
static PhSrvLettura rete_l(void) { return ph_srv_rete(S)->stato; }
static PhSrvLettura media_l(void) { return ph_srv_media(S)->stato_lettura; }

int main(void)
{
    char *tmp = g_dir_make_tmp("yasde-prova-XXXXXX", NULL);
    CONTROLLA(tmp != NULL);
    g_setenv("XDG_CONFIG_HOME", tmp, TRUE);
    g_setenv("XDG_CACHE_HOME", tmp, TRUE);
    ph_lingua_imposta(PH_LINGUA_IT);
    ph_shell_segna_thread_gtk();
    int fd_vero = dup(2);
    err_orig = fdopen(fd_vero, "w");
    char *log_percorso = g_build_filename(tmp, "stderr.txt", NULL);
    CONTROLLA(freopen(log_percorso, "w", stderr) != NULL);

    PhCfgPreferenze pref;
    ph_cfg_preferenze_predefinite(&pref);
    PhErrore e = {{0}};
    finti.connesso = false;
    S = ph_ossatura_servizi_veri(&pref, false, &e);
    CONTROLLA(S != NULL);
    if (S == NULL)
        return 1;
    ph_srv_ascolta(S, PH_SRV_EV_CHIAMATE, ev, NULL);
    ph_srv_ascolta(S, PH_SRV_EV_RETE, ev, NULL);
    ph_srv_ascolta(S, PH_SRV_EV_MEDIA, ev, NULL);
    atomic_store(&sv_prova_sond_periodo_ms, 40);

    /* 1. Nessuno osserva: nessuna lettura, nemmeno col telefono presente. */
    telefono_arriva();
    ASPETTA(atomic_load(&finti.presti) == 1, 4000);
    RESTA(atomic_load(&finti.chi_n) + atomic_load(&finti.rete_n) + atomic_load(&finti.media_n) == 0, 150);
    CONTROLLA(chi_l() == PH_SRV_LET_ATTESA && ev_n[0] + ev_n[1] + ev_n[2] == 0);

    /* 2. Chiamate: valori, cambio, esiti. */
    ph_srv_chiamate_osserva(S, true);
    ASPETTA(chi_l() == PH_SRV_LET_OK, 4000);
    const PhSrvChiamate *ch = ph_srv_chiamate(S);
    CONTROLLA(ch->stato == PH_SRV_LET_OK && ch->n == 2 && ch->v[0].tipo == PH_SRV_CHIAMATA_PERSA);
    CONTROLLA(strcmp(ch->v[0].nome, "Alex Sample") == 0 && strcmp(ch->v[1].numero, "+00 000 000 0002") == 0);
    int n0 = ev_n[0];
    int letture = atomic_load(&finti.chi_n);
    ASPETTA(atomic_load(&finti.chi_n) >= letture + 3, 4000); /* il ritmo c'e' */
    CONTROLLA(atomic_load(&finti.chi_n) >= letture + 3);
    CONTROLLA(ev_n[0] == n0); /* stessa cosa: nessun evento in piu' */
    atomic_store(&finti.chi_marca, 77);
    ASPETTA(ph_srv_chiamate(S)->v[0].durata_s == 77, 4000);
    CONTROLLA(ph_srv_chiamate(S)->v[0].durata_s == 77 && ev_n[0] > n0);
    atomic_store(&finti.chi_esito, 2);
    ASPETTA(chi_l() == PH_SRV_LET_NEGATO, 4000);
    CONTROLLA(chi_l() == PH_SRV_LET_NEGATO && ph_srv_chiamate(S)->n == 0);
    atomic_store(&finti.chi_esito, 1);
    ASPETTA(chi_l() == PH_SRV_LET_NON_DISPONIBILE, 4000);
    CONTROLLA(chi_l() == PH_SRV_LET_NON_DISPONIBILE);
    atomic_store(&finti.chi_esito, 0);
    ASPETTA(chi_l() == PH_SRV_LET_OK, 4000);
    atomic_store(&finti.chi_esito, 3);
    ASPETTA(chi_l() == PH_SRV_LET_NON_DISPONIBILE, 4000);
    CONTROLLA(chi_l() == PH_SRV_LET_NON_DISPONIBILE);
    atomic_store(&finti.chi_esito, 0);
    ASPETTA(chi_l() == PH_SRV_LET_OK, 4000);

    /* 3. Smesso di osservare: nessuna lettura piu'. */
    ph_srv_chiamate_osserva(S, false);
    int fermo = atomic_load(&finti.chi_n);
    n0 = ev_n[0];
    RESTA(atomic_load(&finti.chi_n) <= fermo + 1 && ev_n[0] == n0, 250); /* una lettura gia' in volo, non di piu' */
    fermo = atomic_load(&finti.chi_n);
    RESTA(atomic_load(&finti.chi_n) == fermo, 150);

    /* 4. Rete. */
    ph_srv_rete_osserva(S, true);
    ASPETTA(rete_l() == PH_SRV_LET_OK, 4000);
    const PhSrvReteDettaglio *r = ph_srv_rete(S);
    CONTROLLA(r->wifi == PH_SRV_WIFI_CONNESSO && strcmp(r->wifi_nome, "SampleNet") == 0);
    CONTROLLA(r->mobile == PH_SRV_MOBILE_IN_SERVIZIO && r->generazione == 4 && r->dati_mobili == PH_SRV_TRI_SI && r->aereo == PH_SRV_TRI_NO);
    CONTROLLA(strcmp(r->operatore, "Sample Mobile") == 0);
    atomic_store(&finti.rete_marca, 4);
    ASPETTA(ph_srv_rete(S)->wifi_livello == 4, 4000);
    CONTROLLA(ph_srv_rete(S)->wifi_livello == 4);
    atomic_store(&finti.rete_esito, 2);
    ASPETTA(rete_l() == PH_SRV_LET_NEGATO, 4000);
    CONTROLLA(rete_l() == PH_SRV_LET_NEGATO);
    atomic_store(&finti.rete_esito, 0);
    ASPETTA(rete_l() == PH_SRV_LET_OK, 4000);

    /* 5. Media: valori, comandi (solo 1-3), rilettura subito dopo il comando (periodo lunghissimo). */
    ph_srv_media_osserva(S, true);
    ASPETTA(media_l() == PH_SRV_LET_OK, 4000);
    const PhSrvMedia *m = ph_srv_media(S);
    CONTROLLA(m->stato == PH_SRV_MEDIA_RIPRODUZIONE && strcmp(m->titolo, "Sample Song 0") == 0);
    CONTROLLA(strcmp(m->artista, "Sample Artist") == 0 && m->comandi == 7 && m->pacchetto[0] != '\0');
    ph_srv_media_comando(S, 0);
    ph_srv_media_comando(S, 4);
    ph_srv_media_comando(S, -1);
    RESTA(atomic_load(&finti.media_cmd_n) == 0, 150);
    atomic_store(&sv_prova_sond_periodo_ms, 600000); /* da qui fino alla chiusura: nessuna rilettura periodica */
    ph_srv_media_osserva(S, false);
    ph_srv_media_osserva(S, true); /* rifai: rilettura immediata, poi il periodo lungo */
    /* Valore invariato, periodo lunghissimo: il pezzo comune ha rimesso «in attesa», quindi deve
     * ripubblicare (e rileggere) subito, non restare senza dati per 10 minuti. */
    ASPETTA(media_l() == PH_SRV_LET_OK, 4000);
    CONTROLLA(media_l() == PH_SRV_LET_OK);
    ph_srv_rete_osserva(S, false);
    ph_srv_rete_osserva(S, true);
    ASPETTA(rete_l() == PH_SRV_LET_OK, 4000);
    CONTROLLA(rete_l() == PH_SRV_LET_OK);
    int mn = atomic_load(&finti.media_n);
    RESTA(atomic_load(&finti.media_n) == mn, 150);
    atomic_store(&finti.media_marca, 5);
    ph_srv_media_comando(S, PH_SRV_MEDIA_CMD_AVANTI);
    ASPETTA(strcmp(ph_srv_media(S)->titolo, "Sample Song 5") == 0, 4000);
    CONTROLLA(strcmp(ph_srv_media(S)->titolo, "Sample Song 5") == 0); /* riletto subito, non dopo 10 minuti */
    CONTROLLA(atomic_load(&finti.media_cmd_n) == 1 && atomic_load(&finti.media_cmd_ultimo) == 2);
    atomic_store(&finti.media_cmd_rifiuta, true); /* il telefono lo rifiuta: nessun danno, si rilegge */
    atomic_store(&finti.media_marca, 6);
    ph_srv_media_comando(S, PH_SRV_MEDIA_CMD_INDIETRO);
    ASPETTA(strcmp(ph_srv_media(S)->titolo, "Sample Song 6") == 0, 4000);
    CONTROLLA(atomic_load(&finti.media_cmd_n) == 2 && atomic_load(&finti.media_cmd_ultimo) == 3);
    atomic_store(&finti.media_cmd_rifiuta, false);

    /* 6. Nessuna sessione: tutte «non disponibili» SUBITO (periodi lunghissimi: non alla scadenza);
     * al ritorno, con valori diversi (un altro telefono), si rilegge subito e non resta nulla di prima. */
    ph_srv_chiamate_osserva(S, true);
    ASPETTA(chi_l() == PH_SRV_LET_OK, 4000);
    telefono_parte();
    ASPETTA(chi_l() == PH_SRV_LET_NON_DISPONIBILE && rete_l() == PH_SRV_LET_NON_DISPONIBILE &&
                media_l() == PH_SRV_LET_NON_DISPONIBILE, 6000);
    CONTROLLA(chi_l() == PH_SRV_LET_NON_DISPONIBILE);
    CONTROLLA(rete_l() == PH_SRV_LET_NON_DISPONIBILE);
    CONTROLLA(media_l() == PH_SRV_LET_NON_DISPONIBILE);
    int cmd_n = atomic_load(&finti.media_cmd_n);
    ph_srv_media_comando(S, PH_SRV_MEDIA_CMD_AVANTI); /* nessuna sessione: nessun comando al telefono */
    RESTA(atomic_load(&finti.media_cmd_n) == cmd_n, 150);
    atomic_store(&finti.chi_marca, 11);
    atomic_store(&finti.rete_marca, 1);
    atomic_store(&finti.media_marca, 9);
    telefono_arriva();
    ASPETTA(chi_l() == PH_SRV_LET_OK && rete_l() == PH_SRV_LET_OK && media_l() == PH_SRV_LET_OK, 6000);
    CONTROLLA(chi_l() == PH_SRV_LET_OK && ph_srv_chiamate(S)->v[0].durata_s == 11);
    CONTROLLA(rete_l() == PH_SRV_LET_OK && ph_srv_rete(S)->wifi_livello == 1);
    CONTROLLA(media_l() == PH_SRV_LET_OK && strcmp(ph_srv_media(S)->titolo, "Sample Song 9") == 0);

    /* 7. Chiusura con le tre voci osservate: prestito restituito, nessuna lettura ne' evento dopo. */
    ph_servizi_libera(S);
    CONTROLLA(atomic_load(&finti.presti) == 0);
    int dopo[3] = {atomic_load(&finti.chi_n), atomic_load(&finti.rete_n), atomic_load(&finti.media_n)};
    int ev_dopo[3] = {ev_n[0], ev_n[1], ev_n[2]};
    while (g_main_context_iteration(NULL, FALSE)) {}
    RESTA(atomic_load(&finti.chi_n) == dopo[0] && atomic_load(&finti.rete_n) == dopo[1] &&
              atomic_load(&finti.media_n) == dopo[2], 200);
    CONTROLLA(ev_n[0] == ev_dopo[0] && ev_n[1] == ev_dopo[1] && ev_n[2] == ev_dopo[2]);
    CONTROLLA(nel_gtk_sempre);

    /* Riservatezza. */
    fflush(stderr);
    gchar *log = NULL;
    CONTROLLA(g_file_get_contents(log_percorso, &log, NULL, NULL));
    if (log != NULL) {
        fputs(log, err_orig);
        const char *vietate[] = {"Alex Sample", "SampleNet", "Sample Song", "Sample Artist", "Sample Mobile", "+00 000", "org.sample"};
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
        fprintf(err_orig, "prova_sondaggi_veri: tutto bene\n");
    return errori == 0 ? 0 : 1;
}
