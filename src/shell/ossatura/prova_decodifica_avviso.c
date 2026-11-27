/* Prova dell'AVVISO «decodifica col processore» (sv_flussi.c, via_verifica) sopra pezzi sotto
 * finti (finti_sotto.c). Nessun telefono, nessun hardware: la «via» del decodificatore finto la
 * decide la prova. Controlla, ascoltando PH_SRV_EV_DECODIFICA come fa la shell:
 *  - via hardware: nessun avviso;
 *  - GTK rifiuta il DMA-BUF (caso 2): l'avviso arriva, UNO;
 *  - nella stessa sessione, altri flussi che cadono nel processore (nessuna via hardware: caso 1,
 *    o ripiego a meta': caso 3) non lo ripetono;
 *  - in una sessione di collegamento nuova il caso 1 e poi il caso 3 lo ripresentano;
 *  - l'avviso arriva sul thread GTK; le righe di registro ci sono.
 * Nessun dormi(N): si aspetta un segnale con una scadenza; le verifiche «non arriva nulla» girano
 * per un tempo breve DOPO un segnale positivo (i fotogrammi gia' arrivati). */
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
            fprintf(err_orig, "prova_decodifica_avviso: fallito: %s (riga %d)\n", #c, __LINE__);  \
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

/* Verifica negativa: per `ms` la condizione non deve diventare falsa. */
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

/* ---- Il collegamento finto avvisa da un thread suo -------------------------------------- */
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

/* ---- Cio' che la shell vede ------------------------------------------------------------- */
static int n_avvisi;
static bool avviso_nel_gtk = true;
static void su_decodifica(PhServizi *s, PhSrvEvento ev, const char *arg, void *u)
{
    (void)s; (void)arg; (void)u;
    if (ev != PH_SRV_EV_DECODIFICA)
        return;
    avviso_nel_gtk = avviso_nel_gtk && ph_shell_nel_thread_gtk();
    n_avvisi++;
}

static int n_aperti;
static void cb_flusso(PhSrvFlusso *f, const PhSrvFlussoDati *d, void *u)
{
    (void)f; (void)u;
    if (d->evento == PH_SRV_FL_APERTO)
        n_aperti++;
}

/* Un flusso con il suo thread di lettura (come la superficie). */
typedef struct {
    PhSrvFlusso *f;
    GThread *t;
    atomic_int frames;
} Lettura;

static gpointer lettore_corpo(gpointer p)
{
    Lettura *l = p;
    PhFotogramma fr;
    int r;
    while ((r = ph_srv_flusso_leggi(l->f, &fr, 50)) >= 0) {
        if (r == 1) {
            atomic_fetch_add(&l->frames, 1);
            ph_fotogramma_rilascia(&fr);
        }
    }
    return NULL;
}

/* Apre un flusso, parte il lettore e aspetta i primi fotogrammi (il decodificatore ha lavorato). */
static void apri(PhServizi *s, Lettura *l)
{
    PhErrore e = {{0}};
    PhSrvFlussoRichiesta rq = {"it.finto.uno", 640, 400, 160, 0};
    int aperti = n_aperti;
    l->f = ph_srv_flusso_apri(s, &rq, cb_flusso, NULL, &e);
    CONTROLLA(l->f != NULL);
    if (l->f == NULL)
        return;
    ASPETTA(n_aperti == aperti + 1, 4000);
    CONTROLLA(n_aperti == aperti + 1);
    l->t = g_thread_new("lettore", lettore_corpo, l);
    ASPETTA(atomic_load(&l->frames) >= 5, 4000);
    CONTROLLA(atomic_load(&l->frames) >= 5);
}

static void chiudi(Lettura *l)
{
    if (l->f == NULL)
        return;
    ph_srv_flusso_interrompi(l->f);
    if (l->t != NULL)
        g_thread_join(l->t);
    ph_srv_flusso_chiudi(l->f, false);
    l->f = NULL;
}

/* Quante volte compare `ago` nel testo. */
static int conta(const char *testo, const char *ago)
{
    int n = 0;
    for (const char *p = strstr(testo, ago); p != NULL; p = strstr(p + 1, ago))
        n++;
    return n;
}

int main(void)
{
    char *tmp = g_dir_make_tmp("yasde-prova-XXXXXX", NULL);
    CONTROLLA(tmp != NULL);
    g_setenv("XDG_CONFIG_HOME", tmp, TRUE);
    g_setenv("XDG_CACHE_HOME", tmp, TRUE);
    ph_lingua_imposta(PH_LINGUA_IT);
    ph_shell_segna_thread_gtk();

    /* Lo stderr della prova su un file: alla fine si controllano le righe di registro. */
    int fd_vero = dup(2);
    err_orig = fdopen(fd_vero, "w");
    char *log_percorso = g_build_filename(tmp, "stderr.txt", NULL);
    CONTROLLA(freopen(log_percorso, "w", stderr) != NULL);

    PhCfgPreferenze pref;
    ph_cfg_preferenze_predefinite(&pref);
    PhErrore e = {{0}};
    finti.connesso = false;
    PhServizi *s = ph_ossatura_servizi_veri(&pref, false, &e);
    CONTROLLA(s != NULL);
    if (s == NULL)
        return 1;
    ph_srv_ascolta(s, PH_SRV_EV_DECODIFICA, su_decodifica, NULL);
    atomic_store(&finti.video_pacchetti, true);

    /* ---- Sessione 1. */
    telefono_arriva();
    ASPETTA(atomic_load(&finti.presti) == 1, 6000);
    CONTROLLA(atomic_load(&finti.presti) == 1);

    /* Via hardware (VA-API): nessun avviso. */
    Lettura a = {0}, b = {0}, c = {0};
    apri(s, &a);
    CONTROLLA(atomic_load(&finti.dec_creati) == 1);
    RESTA(n_avvisi == 0, 150);

    /* Caso 2: GTK rifiuta il DMA-BUF -> decodificatore software -> UN avviso. */
    ph_srv_flusso_preferisci_memoria(a.f);
    ASPETTA(n_avvisi >= 1, 4000);
    CONTROLLA(n_avvisi == 1);
    RESTA(n_avvisi == 1, 150); /* non per fotogramma */

    /* Stessa sessione, un altro flusso senza nessuna via hardware (caso 1): niente di nuovo. */
    atomic_store(&finti.dec_senza_hw, true);
    apri(s, &b);
    RESTA(n_avvisi == 1, 150);
    atomic_store(&finti.dec_senza_hw, false);

    /* Stessa sessione, la via hardware rinuncia a meta' (caso 3): niente di nuovo. */
    atomic_store(&finti.dec_ripiego, true);
    apri(s, &c);
    RESTA(n_avvisi == 1, 150);
    atomic_store(&finti.dec_ripiego, false);
    chiudi(&a);
    chiudi(&b);
    chiudi(&c);

    /* ---- Sessione 2: la stessa situazione di prima (caso 1) ripresenta l'avviso. */
    telefono_parte();
    ASPETTA(atomic_load(&finti.presti) == 0, 6000);
    telefono_arriva();
    ASPETTA(atomic_load(&finti.presti) == 1, 6000);
    CONTROLLA(atomic_load(&finti.presti) == 1);
    atomic_store(&finti.dec_senza_hw, true);
    Lettura d = {0};
    apri(s, &d);
    ASPETTA(n_avvisi >= 2, 4000);
    CONTROLLA(n_avvisi == 2);
    RESTA(n_avvisi == 2, 150);
    atomic_store(&finti.dec_senza_hw, false);
    chiudi(&d);

    /* ---- Sessione 3: il ripiego a meta' (caso 3) lo ripresenta, e il decodificatore si
     * ricrea UNA volta come software: da li' i pacchetti non sono piu' errori, quindi
     * nemmeno richieste di fotogramma chiave continue. */
    telefono_parte();
    ASPETTA(atomic_load(&finti.presti) == 0, 6000);
    telefono_arriva();
    ASPETTA(atomic_load(&finti.presti) == 1, 6000);
    CONTROLLA(atomic_load(&finti.presti) == 1);
    atomic_store(&finti.dec_ripiego, true);
    int creati0 = atomic_load(&finti.dec_creati);
    int chiavi0 = atomic_load(&finti.chiavi);
    Lettura g = {0};
    apri(s, &g);
    ASPETTA(n_avvisi >= 3, 4000);
    CONTROLLA(n_avvisi == 3);
    ASPETTA(atomic_load(&finti.dec_creati) >= creati0 + 2, 4000);
    CONTROLLA(atomic_load(&finti.dec_creati) == creati0 + 2); /* nato hardware, poi UNO software */
    ASPETTA(atomic_load(&finti.chiavi) > chiavi0, 4000);      /* il chiave chiesto alla ricreazione */
    int chiavi1 = atomic_load(&finti.chiavi);
    /* Senza la ricreazione ph_decodifica_dai tornerebbe errore a ogni pacchetto e un chiave si
     * chiederebbe ogni 500 ms: in 1,3 s non ne deve arrivare piu' di uno oltre a quello gia' contato. */
    RESTA(atomic_load(&finti.chiavi) <= chiavi1 + 1 && atomic_load(&finti.dec_creati) == creati0 + 2 && n_avvisi == 3,
          1300);
    atomic_store(&finti.dec_ripiego, false);
    chiudi(&g);

    /* ---- Sessione 3 ancora: un flusso con la via hardware sana, nessun avviso in piu'. */
    Lettura h = {0};
    apri(s, &h);
    RESTA(n_avvisi == 3, 150);

    /* ---- Sessione 4: lo STESSO flusso h, dopo la riapertura, ricade subito nel processore: l'avviso
     * si ripresenta e la causa nel registro e' «nessuna via hardware», non «non regge il flusso»
     * (h era nato con una via hardware, ma il nuovo decodificatore e' software gia' alla creazione). */
    telefono_parte();
    ASPETTA(atomic_load(&finti.presti) == 0, 6000);
    atomic_store(&finti.dec_senza_hw, true);
    telefono_arriva();
    ASPETTA(atomic_load(&finti.presti) == 1, 6000);
    ASPETTA(n_avvisi >= 4, 6000);
    CONTROLLA(n_avvisi == 4);
    RESTA(n_avvisi == 4, 150);
    atomic_store(&finti.dec_senza_hw, false);
    chiudi(&h);

    CONTROLLA(avviso_nel_gtk);
    ph_servizi_libera(s);

    /* ---- Registro: le righe che il manuale tecnico cita, e quante volte. */
    fflush(stderr);
    gchar *log = NULL;
    CONTROLLA(g_file_get_contents(log_percorso, &log, NULL, NULL));
    if (log != NULL) {
        CONTROLLA(strstr(log, "decodifica video - causa: via vaapi (scheda grafica)") != NULL);
        CONTROLLA(conta(log, "decodifica col processore - causa: GTK ha rifiutato il DMA-BUF: prestazioni ridotte") == 1);
        CONTROLLA(conta(log, "decodifica col processore - causa: nessuna via hardware disponibile: prestazioni ridotte") == 3);
        CONTROLLA(conta(log, "decodifica col processore - causa: la via hardware non regge il flusso: prestazioni ridotte") == 2);
        g_free(log);
    }
    char *cmd = g_strdup_printf("rm -rf '%s'", tmp);
    CONTROLLA(system(cmd) == 0);
    g_free(cmd);
    g_free(log_percorso);
    g_free(tmp);
    if (errori == 0)
        fprintf(err_orig, "prova_decodifica_avviso: tutto bene\n");
    return errori == 0 ? 0 : 1;
}
