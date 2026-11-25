/* Strumento di misura (non fa parte dell'app): apre insieme una sessione
 * video per ogni app indicata sul telefono vero e misura, per ogni flusso,
 * quanti fotogrammi arrivano, quanta banda usano e quanto sono regolari.
 * Risponde al punto aperto A3 della specifica (4.12): quanti codificatori in
 * parallelo regge il telefono. Tutti i numeri stampati sono misurati in
 * questa esecuzione, su questo telefono, e vanno citati con le condizioni
 * (misura, bit al secondo, via di decodifica).
 * Ogni errore: la frase di PhErrore su stderr. */
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <libavutil/frame.h>

#include "adb/adb.h"
#include "componente/componente.h"
#include "scoperta/scoperta.h"
#include "video/decodifica.h"

#define TEMPO_CONNESSIONE_MS 15000
#define MAX_FLUSSI 16
#define SCARTO_INIZIALE_S 1.0   /* secondi scartati dopo il primo fotogramma di ogni flusso */
#define SOGLIA_MOVIMENTO_MS 100.0 /* intervalli sotto questa soglia = flusso in movimento */
#define DISTANZA_APERTURE_MS 500
#define PASSI_SCORRIMENTO 20
#define PASSO_MOVIMENTO_MS 16
#define PERIODO_TELEFONO_S 5.0
#define ATTESA_AVVISO_S 3.0   /* dopo tanti secondi senza fotogrammi si avvisa subito */

static const char *g_chiave = NULL;
static const char *g_telefono = NULL;
static atomic_bool g_sigint = false;  /* alzato dal gestore del segnale */
static atomic_bool g_ferma = false;   /* fine del tempo, SIGINT o guasto: tutti i thread si fermano */
static _Atomic double g_fine_misura = 0; /* scritto prima di alzare g_ferma */

static double ora_s(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + (double)t.tv_nsec / 1e9;
}

static void dormi_ms(int ms)
{
    struct timespec t = {ms / 1000, (long)(ms % 1000) * 1000000L};
    nanosleep(&t, NULL);
}

/* Il gestore del segnale alza solo il flag: niente altro lavoro. */
static void su_sigint(int sig)
{
    (void)sig;
    atomic_store(&g_sigint, true);
}

/* ---- Flusso ---------------------------------------------------------------*/
typedef struct {
    const char *pacchetto;
    PhVideoFlusso *f;
    atomic_uint id;
    atomic_uint ultimo_evento;     /* ultimo evento del telefono per questa sessione (0 = nessuno) */
    atomic_bool primo_ricevuto;
    bool avvisato;                 /* avviso di assenza di fotogrammi gia' dato */
    pthread_t thread;
    bool thread_avviato;
    PhVia via;
    bool decodifica;
    uint16_t larghezza, altezza;   /* misure della richiesta (poi dei pacchetti) */
    atomic_uint dim;               /* larghezza<<16 | altezza, per il thread dei tocchi */
    atomic_bool vivo;
    /* Scritti solo dal thread del flusso; letti dopo la join. */
    double t_richiesta, t_primo, t_fine;
    char errore[256];              /* non vuoto = il flusso e' caduto */
    char nota[128];                /* esito non fatale (flusso finito dal telefono) */
    unsigned fotogrammi, chiavi, pause, err_dec, dec_n;
    uint64_t byte;
    double dec_totale_ms;
    double *intervalli;            /* ms, solo i tratti in movimento */
    size_t n_int, cap_int;
    double *tel, *rete;            /* ms: intervalli tra tempi del telefono; arrivo - tempo telefono */
    size_t n_tel, cap_tel, n_rete, cap_rete;
    PhVia via_vera;
} Flusso;

static Flusso g_fl[MAX_FLUSSI];
static int g_n_fl = 0;

static int aggiungi_valore(double **v, size_t *n, size_t *cap, double ms)
{
    if (*n == *cap) {
        size_t nc = *cap != 0 ? *cap * 2 : 4096;
        double *nv = realloc(*v, nc * sizeof **v);
        if (nv == NULL)
            return -1;
        *v = nv;
        *cap = nc;
    }
    (*v)[(*n)++] = ms;
    return 0;
}

static int aggiungi_intervallo(Flusso *x, double ms)
{
    return aggiungi_valore(&x->intervalli, &x->n_int, &x->cap_int, ms);
}

static void *thread_flusso(void *arg)
{
    Flusso *x = arg;
    PhErrore e = {{0}};
    PhDecodifica *d = NULL;
    if (x->decodifica) {
        d = ph_decodifica_crea(x->via, &e);
        if (d == NULL) {
            snprintf(x->errore, sizeof x->errore, "decodificatore non creato: %s", e.testo);
            x->t_fine = ora_s();
            atomic_store(&x->vivo, false);
            return NULL;
        }
        x->via_vera = ph_decodifica_via(d);
    }
    bool primo_visto = false, in_finestra = false;
    double inizio_finestra = 0, ultimo_arrivo = 0;
    uint64_t ultimo_tel_us = 0;
    bool caduto = false;
    while (!atomic_load(&g_ferma)) {
        PhVideoPacchetto p;
        int n = ph_video_leggi(x->f, &p, 200, &e);
        if (n == PH_CANALE_TEMPO)
            continue;
        if (n < 0) {
            snprintf(x->errore, sizeof x->errore, "lettura fallita: %s", e.testo);
            caduto = true;
            break;
        }
        if (n == 0) {
            snprintf(x->nota, sizeof x->nota, "flusso finito dal telefono");
            caduto = true;
            break;
        }
        double adesso = ora_s();
        if (p.larghezza > 0 && p.altezza > 0)
            atomic_store(&x->dim, ((unsigned)p.larghezza << 16) | p.altezza);
        if (!p.configurazione && !primo_visto) {
            primo_visto = true;
            x->t_primo = adesso;
            atomic_store(&x->primo_ricevuto, true);
            inizio_finestra = adesso + SCARTO_INIZIALE_S;
        }
        /* Il decodificatore riceve tutto, anche nel secondo scartato. */
        double dec_ms = 0;
        int errdec = 0;
        if (d != NULL) {
            double t0 = ora_s();
            if (ph_decodifica_dai(d, p.configurazione, p.dati, p.lunghezza, p.tempo_us, &e) != 0) {
                errdec++;
            } else {
                for (;;) {
                    AVFrame *fr = NULL;
                    int r = ph_decodifica_prendi(d, &fr, &e);
                    if (r < 0) {
                        errdec++;
                        break;
                    }
                    if (r == 0)
                        break;
                    av_frame_free(&fr);
                }
            }
            dec_ms = (ora_s() - t0) * 1000.0;
        }
        if (!primo_visto || adesso < inizio_finestra)
            continue;
        x->byte += p.lunghezza;
        x->err_dec += (unsigned)errdec;
        if (p.configurazione)
            continue;
        x->fotogrammi++;
        if (p.chiave)
            x->chiavi++;
        if (d != NULL) {
            x->dec_totale_ms += dec_ms;
            x->dec_n++;
        }
        /* Tempo del telefono: confronta la regolarita' alla partenza con quella all'arrivo. */
        if (aggiungi_valore(&x->rete, &x->n_rete, &x->cap_rete,
                            adesso * 1000.0 - (double)p.tempo_us / 1000.0) != 0) {
            snprintf(x->errore, sizeof x->errore, "memoria esaurita");
            caduto = true;
            break;
        }
        if (in_finestra) {
            if (p.tempo_us >= ultimo_tel_us && (double)(p.tempo_us - ultimo_tel_us) / 1000.0 < SOGLIA_MOVIMENTO_MS &&
                aggiungi_valore(&x->tel, &x->n_tel, &x->cap_tel,
                                (double)(p.tempo_us - ultimo_tel_us) / 1000.0) != 0) {
                snprintf(x->errore, sizeof x->errore, "memoria esaurita");
                caduto = true;
                break;
            }
            double ms = (adesso - ultimo_arrivo) * 1000.0;
            if (ms < SOGLIA_MOVIMENTO_MS) {
                if (aggiungi_intervallo(x, ms) != 0) {
                    snprintf(x->errore, sizeof x->errore, "memoria esaurita");
                    caduto = true;
                    break;
                }
            } else {
                x->pause++;
            }
        }
        in_finestra = true;
        ultimo_arrivo = adesso;
        ultimo_tel_us = p.tempo_us;
    }
    double fine = atomic_load(&g_fine_misura);
    x->t_fine = (caduto || fine <= 0) ? ora_s() : fine;
    if (fine > 0 && x->t_fine > fine)
        x->t_fine = fine;
    atomic_store(&x->vivo, false);
    if (d != NULL)
        ph_decodifica_libera(d);
    return NULL;
}

/* ---- Movimento: scorrimento su tutte le sessioni, insieme o a turno -------*/
typedef struct {
    PhComponente *c;
    bool a_turno;   /* true = un flusso alla volta; false = tutti insieme */
} ArgMovimento;

enum { GESTO_VERTICALE = 0, GESTO_ORIZZONTALE, GESTO_MISTO };
static int g_gesto = GESTO_VERTICALE;

/* Il gesto numero n e' orizzontale? Il misto alterna i due tipi. */
static bool gesto_orizzontale(unsigned n)
{
    return g_gesto == GESTO_ORIZZONTALE || (g_gesto == GESTO_MISTO && n % 2 == 1);
}

/* Verso del gesto n: alterna a ogni gesto (nel misto a ogni coppia, cosi'
 * ciascun tipo di gesto alterna il suo). */
static bool gesto_inverso(unsigned n)
{
    return (g_gesto == GESTO_MISTO ? n / 2 : n) % 2 == 1;
}

/* Un evento di tocco per il flusso x, al passo k di PASSI_SCORRIMENTO: lo
 * scorrimento va dal 75% al 25% della dimensione (o all'inverso), sull'altra
 * dimensione il dito sta a meta'. Torna 0 o -1. */
static int tocco_flusso(PhComponente *c, const Flusso *x, uint8_t azione, unsigned gesto, int k,
                        PhErrore *e)
{
    unsigned dim = atomic_load(&x->dim);
    uint16_t w = (uint16_t)(dim >> 16), h = (uint16_t)(dim & 0xffff);
    bool orizz = gesto_orizzontale(gesto);
    int lungo = orizz ? w : h;
    int da = gesto_inverso(gesto) ? lungo / 4 : lungo * 3 / 4;
    int fino = gesto_inverso(gesto) ? lungo * 3 / 4 : lungo / 4;
    int v = da + (fino - da) * k / PASSI_SCORRIMENTO;
    uint16_t px = (uint16_t)(orizz ? v : w / 2), py = (uint16_t)(orizz ? h / 2 : v);
    return ph_input_tocco(c, atomic_load(&x->id), w, h, azione, px, py, 0, e);
}

/* Tutti i flussi scorrono insieme e di continuo: i codificatori lavorano
 * nello stesso momento, che e' il caso da misurare. */
static void *thread_movimento_insieme(void *arg)
{
    ArgMovimento *a = arg;
    bool segnalato = false;
    unsigned gesto = 0;
    while (!atomic_load(&g_ferma)) {
        bool fatto = false;
        PhErrore e = {{0}};
        for (int k = 0; k <= PASSI_SCORRIMENTO + 1 && !atomic_load(&g_ferma); k++) {
            uint8_t az = k == 0 ? PH_TOCCO_GIU : k > PASSI_SCORRIMENTO ? PH_TOCCO_SU : PH_TOCCO_MUOVI;
            int kk = k > PASSI_SCORRIMENTO ? PASSI_SCORRIMENTO : k;
            if (k > 0)
                dormi_ms(PASSO_MOVIMENTO_MS);
            for (int i = 0; i < g_n_fl; i++) {
                Flusso *x = &g_fl[i];
                if (x->f == NULL || !atomic_load(&x->vivo))
                    continue;
                fatto = true;
                if (tocco_flusso(a->c, x, az, gesto, kk, &e) != 0 && !segnalato) {
                    segnalato = true;
                    fprintf(stderr, "movimento: input non inviato: %s\n", e.testo);
                }
            }
        }
        gesto++;
        if (!fatto)
            dormi_ms(50);
    }
    return NULL;
}

/* Un flusso alla volta (per confronto: uno in primo piano che si muove e gli
 * altri fermi). */
static void *thread_movimento_a_turno(void *arg)
{
    ArgMovimento *a = arg;
    unsigned gesti[MAX_FLUSSI] = {0};
    bool segnalato = false;
    while (!atomic_load(&g_ferma)) {
        bool fatto = false;
        for (int i = 0; i < g_n_fl && !atomic_load(&g_ferma); i++) {
            Flusso *x = &g_fl[i];
            if (x->f == NULL || !atomic_load(&x->vivo))
                continue;
            fatto = true;
            unsigned gesto = gesti[i]++;
            PhErrore e = {{0}};
            int r = tocco_flusso(a->c, x, PH_TOCCO_GIU, gesto, 0, &e);
            for (int k = 1; r == 0 && k <= PASSI_SCORRIMENTO; k++) {
                dormi_ms(PASSO_MOVIMENTO_MS);
                r = tocco_flusso(a->c, x, PH_TOCCO_MUOVI, gesto, k, &e);
            }
            if (r == 0)
                r = tocco_flusso(a->c, x, PH_TOCCO_SU, gesto, PASSI_SCORRIMENTO, &e);
            if (r != 0 && !segnalato) {
                segnalato = true;
                fprintf(stderr, "movimento: input non inviato: %s\n", e.testo);
            }
        }
        if (!fatto)
            dormi_ms(50);
    }
    return NULL;
}

/* ---- Temperatura e carico del telefono ------------------------------------*/
typedef struct {
    bool ok;              /* temperatura letta */
    double temperatura;   /* gradi C */
    bool carico_ok;
    char carico[64];      /* i primi tre numeri di /proc/loadavg, come sono */
} StatoTelefono;

/* Esegue un comando di shell e mette in riga la prima riga dell'uscita
 * (senza a capo). false se il comando non e' riuscito. */
static bool shell_prima_riga(PhAdb *a, const char *comando, char *riga, size_t cap)
{
    PhBuffer out = PH_BUFFER_VUOTO, err = PH_BUFFER_VUOTO;
    PhErrore e = {{0}};
    int codice = 0;
    bool ok = false;
    if (ph_shell_esegui(a, comando, &out, &err, &codice, 10000, &e) == 0 && codice == 0 &&
        out.dati != NULL && out.lunghezza > 0) {
        size_t n = 0;
        while (n < out.lunghezza && out.dati[n] != '\n' && out.dati[n] != '\r')
            n++;
        if (n >= cap)
            n = cap - 1;
        memcpy(riga, out.dati, n);
        riga[n] = '\0';
        ok = true;
    }
    ph_buffer_libera(&out);
    ph_buffer_libera(&err);
    return ok;
}

static StatoTelefono leggi_telefono(PhAdb *a)
{
    StatoTelefono s = {false, 0, false, ""};
    char riga[160];
    /* grep stampa solo le righe con "temperature": se ne prende la prima. */
    if (shell_prima_riga(a, "dumpsys battery | grep temperature", riga, sizeof riga)) {
        const char *t = strstr(riga, "temperature:");
        int decimi = 0;
        if (t != NULL && sscanf(t + 12, "%d", &decimi) == 1) {
            s.temperatura = decimi / 10.0;
            s.ok = true;
        }
    }
    if (shell_prima_riga(a, "head -n 1 /proc/loadavg", riga, sizeof riga)) {
        /* Primi tre campi separati da spazi, senza interpretarli. */
        char *salva = NULL;
        char *tok = strtok_r(riga, " \t", &salva);
        size_t len = 0;
        for (int k = 0; k < 3 && tok != NULL; k++, tok = strtok_r(NULL, " \t", &salva)) {
            int w = snprintf(s.carico + len, sizeof s.carico - len, "%s%s", k > 0 ? " " : "", tok);
            if (w < 0 || (size_t)w >= sizeof s.carico - len)
                break;
            len += (size_t)w;
            s.carico_ok = true;
        }
    }
    return s;
}

typedef struct {
    PhAdb *a;
    double t0;
    StatoTelefono ultimo;
} ArgMonitor;

static void *thread_monitor(void *arg)
{
    ArgMonitor *m = arg;
    double prossimo = m->t0 + PERIODO_TELEFONO_S;
    while (!atomic_load(&g_ferma)) {
        if (ora_s() < prossimo) {
            dormi_ms(100);
            continue;
        }
        prossimo += PERIODO_TELEFONO_S;
        StatoTelefono s = leggi_telefono(m->a);
        m->ultimo = s;
        printf("%3.0f s: ", ora_s() - m->t0);
        if (s.ok)
            printf("batteria %.1f C", s.temperatura);
        else
            printf("batteria non letta");
        printf(", carico (grezzo) %s\n", s.carico_ok ? s.carico : "non leggibile");
        fflush(stdout);
    }
    return NULL;
}

/* ---- Eventi del telefono ---------------------------------------------------*/
static void evento(uint32_t id, uint8_t che_cosa, uint16_t a, uint16_t b, void *utente)
{
    (void)utente;
    for (int i = 0; i < g_n_fl; i++)
        if (g_fl[i].f != NULL && atomic_load(&g_fl[i].id) == id)
            atomic_store(&g_fl[i].ultimo_evento, che_cosa);
    switch (che_cosa) {
    case 1:
        fprintf(stderr, "evento: video %u, l'app si e' chiusa da sola\n", id);
        break;
    case 2:
        fprintf(stderr, "evento: video %u, schermo protetto %s\n", id, a != 0 ? "acceso" : "spento");
        break;
    case 3:
        fprintf(stderr, "evento: video %u, misure cambiate in %ux%u\n", id, a, b);
        break;
    case 4:
        fprintf(stderr, "evento: video %u, errore del codificatore (sessione morta)\n", id);
        break;
    default:
        fprintf(stderr, "evento: video %u tipo %u (%u, %u)\n", id, che_cosa, a, b);
        break;
    }
}

/* ---- Collegamento (come yasde-prova, ma senza stampare nomi) ----------*/
static int dividi_host_porta(const char *s, char *host, size_t cap, uint16_t *porta)
{
    const char *due = strrchr(s, ':');
    if (due == NULL || due == s || (size_t)(due - s) >= cap)
        return -1;
    char *fine = NULL;
    long p = strtol(due + 1, &fine, 10);
    if (*(due + 1) == '\0' || *fine != '\0' || p <= 0 || p > 65535)
        return -1;
    memcpy(host, s, (size_t)(due - s));
    host[due - s] = '\0';
    *porta = (uint16_t)p;
    return 0;
}

static PhChiave *carica_chiave(PhErrore *e)
{
    char percorso[1024];
    if (g_chiave != NULL) {
        snprintf(percorso, sizeof percorso, "%s", g_chiave);
    } else {
        const char *casa = getenv("HOME");
        if (casa == NULL) {
            ph_errore(e, "variabile HOME assente: indicare la chiave con --chiave");
            return NULL;
        }
        snprintf(percorso, sizeof percorso, "%s/.config/YASDE/chiave.pem", casa);
    }
    return ph_chiave_carica_o_crea(percorso, e);
}

static PhAdb *collega(const PhChiave *chiave, PhErrore *e)
{
    char host[64];
    uint16_t porta = 0;
    if (g_telefono != NULL) {
        if (dividi_host_porta(g_telefono, host, sizeof host, &porta) != 0) {
            ph_errore(e, "telefono non valido: serve HOST:PORTA");
            return NULL;
        }
    } else {
        PhTelefonoTrovato t[16];
        int n = ph_scoperta_cerca(5000, t, 16, e);
        if (n < 0)
            return NULL;
        int scelto = -1;
        for (int i = 0; i < n && scelto < 0; i++)
            if (!t[i].abbinamento)
                scelto = i;
        if (scelto < 0) {
            ph_errore(e, "nessun telefono con il debug wireless trovato in rete");
            return NULL;
        }
        snprintf(host, sizeof host, "%s", t[scelto].indirizzo);
        porta = t[scelto].porta;
    }
    bool non_abbinato = false;
    PhAdb *a = ph_adb_collega(host, porta, chiave, TEMPO_CONNESSIONE_MS, &non_abbinato, e);
    if (a == NULL && non_abbinato)
        ph_errore_contesto(e, "PC non abbinato: usare il comando abbina di yasde-prova");
    return a;
}

/* ---- Statistiche -----------------------------------------------------------*/
static int cmp_double(const void *a, const void *b)
{
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

/* Percentile per rango piu' vicino su un vettore gia' ordinato. */
static double percentile(const double *v, size_t n, unsigned p)
{
    if (n == 0)
        return 0;
    size_t i = (n * p + 99) / 100;
    if (i < 1)
        i = 1;
    if (i > n)
        i = n;
    return v[i - 1];
}

static void stampa_tabella(double secondi_richiesti, bool decodifica)
{
    double tot_mbit = 0, tot_fps = 0;
    printf("\nflusso  pacchetto                        primo   fotogr   fps  Mbit/s"
           "   mediana    p95    p99    max  pause  chiavi  moto%%\n");
    printf("                                          (ms)                          (ms)"
           "   (ms)   (ms)   (ms)\n");
    for (int i = 0; i < g_n_fl; i++) {
        Flusso *x = &g_fl[i];
        if (x->f == NULL) {
            printf("%-7d %-30.30s  NON APERTO: %s\n", i + 1, x->pacchetto, x->errore);
            continue;
        }
        double durata = x->t_fine - (x->t_primo + SCARTO_INIZIALE_S);
        unsigned ev = atomic_load(&x->ultimo_evento);
        if (!atomic_load(&x->primo_ricevuto)) {
            printf("%-7d %-30.30s  NESSUN FOTOGRAMMA dopo %.0f ms di attesa (ultimo evento del telefono: %u)%s%s\n",
                   i + 1, x->pacchetto, (x->t_fine - x->t_richiesta) * 1000.0, ev,
                   x->errore[0] != '\0' ? ": " : "", x->errore);
            continue;
        }
        if (durata <= 0 || x->fotogrammi == 0) {
            printf("%-7d %-30.30s  NESSUN FOTOGRAMMA nella finestra di misura (il primo e' arrivato dopo "
                   "%.0f ms; ultimo evento del telefono: %u)%s%s\n", i + 1, x->pacchetto,
                   (x->t_primo - x->t_richiesta) * 1000.0, ev, x->errore[0] != '\0' ? ": " : "",
                   x->errore);
            continue;
        }
        double somma_moto = 0;
        for (size_t q = 0; q < x->n_int; q++)
            somma_moto += x->intervalli[q];
        qsort(x->intervalli, x->n_int, sizeof *x->intervalli, cmp_double);
        qsort(x->tel, x->n_tel, sizeof *x->tel, cmp_double);
        qsort(x->rete, x->n_rete, sizeof *x->rete, cmp_double);
        double fps = (double)x->fotogrammi / durata;
        double mbit = (double)x->byte * 8.0 / durata / 1e6;
        tot_mbit += mbit;
        tot_fps += fps;
        printf("%-7d %-30.30s %7.0f %8u %5.1f %7.2f %9.1f %6.1f %6.1f %6.1f %6u %7u %6.1f%s%s\n", i + 1,
               x->pacchetto, (x->t_primo - x->t_richiesta) * 1000.0, x->fotogrammi, fps, mbit,
               percentile(x->intervalli, x->n_int, 50), percentile(x->intervalli, x->n_int, 95),
               percentile(x->intervalli, x->n_int, 99),
               x->n_int > 0 ? x->intervalli[x->n_int - 1] : 0.0, x->pause, x->chiavi,
               100.0 * somma_moto / 1000.0 / durata,
               x->errore[0] != '\0' ? "  CADUTO: " : "", x->errore);
    }
    printf("\nregolarita' alla partenza (tempo del telefono) e all'arrivo, solo intervalli sotto %.0f ms\n"
           "flusso   arrivo: mediana    p95    p99    max   telefono: mediana    p95    p99    max"
           "   varianza di rete (ms)\n", SOGLIA_MOVIMENTO_MS);
    for (int i = 0; i < g_n_fl; i++) {
        Flusso *x = &g_fl[i];
        if (x->f == NULL || x->fotogrammi == 0 || x->n_rete == 0)
            continue;
        printf("%-7d %16.1f %6.1f %6.1f %6.1f %20.1f %6.1f %6.1f %6.1f %23.1f\n", i + 1,
               percentile(x->intervalli, x->n_int, 50), percentile(x->intervalli, x->n_int, 95),
               percentile(x->intervalli, x->n_int, 99), x->n_int > 0 ? x->intervalli[x->n_int - 1] : 0.0,
               percentile(x->tel, x->n_tel, 50), percentile(x->tel, x->n_tel, 95),
               percentile(x->tel, x->n_tel, 99), x->n_tel > 0 ? x->tel[x->n_tel - 1] : 0.0,
               percentile(x->rete, x->n_rete, 99) - x->rete[0]);
    }
    printf("(varianza di rete = 99° percentile meno minimo di [arrivo - tempo del telefono] per "
           "fotogramma: quanto i pacchetti arrivano in ritardo rispetto al piu' puntuale)\n");
    printf("\ntotale: %.2f Mbit/s, %.1f fotogrammi/s (somma dei flussi, finestra di %.0f s meno "
           "il primo secondo di ognuno)\n", tot_mbit, tot_fps, secondi_richiesti);
    printf("(mediana, percentili e massimo solo sugli intervalli sotto %.0f ms; \"pause\" = "
           "intervalli piu' lunghi)\n", SOGLIA_MOVIMENTO_MS);
    if (decodifica) {
        printf("\nflusso  via        errori di decodifica   tempo medio di decodifica (ms)\n");
        for (int i = 0; i < g_n_fl; i++) {
            Flusso *x = &g_fl[i];
            if (x->f == NULL || x->dec_n == 0)
                continue;
            printf("%-7d %-10s %21u   %.2f\n", i + 1, ph_via_nome(x->via_vera), x->err_dec,
                   x->dec_totale_ms / (double)x->dec_n);
        }
    }
    for (int i = 0; i < g_n_fl; i++)
        if (g_fl[i].nota[0] != '\0')
            printf("nota: flusso %d: %s\n", i + 1, g_fl[i].nota);
}

/* ---- Principale ------------------------------------------------------------*/
static void uso(void)
{
    fprintf(stderr,
            "uso: misura-flussi [--chiave FILE] [--telefono HOST:PORTA] [--secondi N]\n"
            "       [--misura LxA] [--bit BIT_AL_SECONDO] [--via automatica|vaapi|vulkan|software|nessuna]\n"
            "       [--movimento | --movimento-a-turno]\n       [--gesto verticale|orizzontale|misto] JAR PACCHETTO [PACCHETTO...]\n");
}

int main(int argc, char **argv)
{
    int secondi = 20;
    unsigned w = 1280, h = 720;
    unsigned long bit = 8000000;
    bool decodifica = true, movimento = false, a_turno = false;
    PhVia via = PH_VIA_AUTOMATICA;
    int i = 1;
    for (; i < argc && argv[i][0] == '-'; i++) {
        const char *o = argv[i];
        bool ha_valore = i + 1 < argc;
        if (strcmp(o, "--movimento") == 0) {
            movimento = true;
        } else if (strcmp(o, "--movimento-a-turno") == 0) {
            movimento = true;
            a_turno = true;
        } else if (strcmp(o, "--gesto") == 0 && ha_valore) {
            const char *v = argv[++i];
            if (strcmp(v, "verticale") == 0)
                g_gesto = GESTO_VERTICALE;
            else if (strcmp(v, "orizzontale") == 0)
                g_gesto = GESTO_ORIZZONTALE;
            else if (strcmp(v, "misto") == 0)
                g_gesto = GESTO_MISTO;
            else {
                fprintf(stderr, "errore: --gesto vuole verticale, orizzontale o misto\n");
                return 1;
            }
        } else if (strcmp(o, "--chiave") == 0 && ha_valore) {
            g_chiave = argv[++i];
        } else if (strcmp(o, "--telefono") == 0 && ha_valore) {
            g_telefono = argv[++i];
        } else if (strcmp(o, "--secondi") == 0 && ha_valore) {
            secondi = atoi(argv[++i]);
            if (secondi <= 2) {
                fprintf(stderr, "errore: --secondi deve essere almeno 3\n");
                return 1;
            }
        } else if (strcmp(o, "--misura") == 0 && ha_valore) {
            if (sscanf(argv[++i], "%ux%u", &w, &h) != 2 || w < 64 || h < 64 || w > 2560 ||
                h > 2560 || w % 8 != 0 || h % 8 != 0) {
                fprintf(stderr, "errore: --misura vuole LxA, multipli di 8 tra 64 e 2560\n");
                return 1;
            }
        } else if (strcmp(o, "--bit") == 0 && ha_valore) {
            bit = strtoul(argv[++i], NULL, 10);
            if (bit == 0 || bit > 100000000UL) {
                fprintf(stderr, "errore: --bit non valido\n");
                return 1;
            }
        } else if (strcmp(o, "--via") == 0 && ha_valore) {
            const char *v = argv[++i];
            decodifica = true;
            if (strcmp(v, "automatica") == 0)
                via = PH_VIA_AUTOMATICA;
            else if (strcmp(v, "vaapi") == 0)
                via = PH_VIA_VAAPI;
            else if (strcmp(v, "vulkan") == 0)
                via = PH_VIA_VULKAN;
            else if (strcmp(v, "software") == 0)
                via = PH_VIA_SOFTWARE;
            else if (strcmp(v, "nessuna") == 0)
                decodifica = false;
            else {
                fprintf(stderr, "errore: --via sconosciuta: %s\n", v);
                return 1;
            }
        } else {
            fprintf(stderr, "errore: opzione sconosciuta o senza valore: %s\n", o);
            uso();
            return 1;
        }
    }
    if (argc - i < 2) {
        uso();
        return 1;
    }
    const char *jar = argv[i++];
    int numero = argc - i;
    if (numero > MAX_FLUSSI) {
        fprintf(stderr, "errore: al massimo %d pacchetti\n", MAX_FLUSSI);
        return 1;
    }
    for (int k = 0; k < numero; k++) {
        g_fl[k].pacchetto = argv[i + k];
        atomic_init(&g_fl[k].vivo, false);
        atomic_init(&g_fl[k].dim, 0);
    }
    g_n_fl = numero;

    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = su_sigint;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);

    PhErrore e = {{0}};
    PhChiave *k = carica_chiave(&e);
    PhAdb *a = k != NULL ? collega(k, &e) : NULL;
    if (a == NULL) {
        fprintf(stderr, "errore: %s\n", e.testo[0] != '\0' ? e.testo : "collegamento fallito");
        if (k != NULL)
            ph_chiave_libera(k);
        return 1;
    }
    int uscita = 1;
    PhTelefonoInfo info;
    memset(&info, 0, sizeof info);
    PhComponente *c = ph_componente_avvia(a, jar, evento, NULL, &info, 60000, &e);
    if (c == NULL) {
        fprintf(stderr, "errore: componente non avviato: %s\n", e.testo);
        goto fine;
    }
    printf("telefono: %s, api %u, schermo %ux%u, %u dpi\n", info.modello, info.api,
           info.larghezza, info.altezza, info.densita);
    printf("misura: %u flussi %ux%u, %lu bit/s, 60 fps richiesti, decodifica %s, movimento %s, gesto %s, %d s\n",
           (unsigned)numero, w, h, bit, decodifica ? ph_via_nome(via) : "nessuna",
           movimento ? (a_turno ? "a turno" : "tutti insieme") : "no",
           g_gesto == GESTO_VERTICALE ? "verticale" : g_gesto == GESTO_ORIZZONTALE ? "orizzontale" : "misto",
           secondi);

    StatoTelefono iniziale = leggi_telefono(a);
    if (iniziale.ok)
        printf("batteria iniziale %.1f C", iniziale.temperatura);
    else
        printf("batteria iniziale non letta");
    printf(", carico (grezzo) %s\n", iniziale.carico_ok ? iniziale.carico : "non leggibile");
    fflush(stdout);

    /* Aperture una alla volta; il thread di lettura parte subito per non
     * perdere pacchetti e per misurare il tempo fino al primo fotogramma. */
    for (int n = 0; n < numero && !atomic_load(&g_sigint); n++) {
        Flusso *x = &g_fl[n];
        PhVideoRichiesta r = {.larghezza = (uint16_t)w, .altezza = (uint16_t)h,
                              .densita = info.densita, .bit_al_secondo = (uint32_t)bit,
                              .fps = 60, .pacchetto = x->pacchetto};
        x->decodifica = decodifica;
        x->via = via;
        x->t_richiesta = ora_s();
        x->f = ph_video_apri(c, &r, &e);
        if (x->f == NULL) {
            snprintf(x->errore, sizeof x->errore, "%s", e.testo);
            fprintf(stderr, "flusso %d (%s) non aperto: %s\n", n + 1, x->pacchetto, e.testo);
        } else {
            atomic_store(&x->id, ph_video_id(x->f));
            atomic_store(&x->dim, (w << 16) | h);
            atomic_store(&x->vivo, true);
            if (pthread_create(&x->thread, NULL, thread_flusso, x) != 0) {
                snprintf(x->errore, sizeof x->errore, "thread non creato");
                atomic_store(&x->vivo, false);
            } else {
                x->thread_avviato = true;
            }
        }
        if (n + 1 < numero)
            dormi_ms(DISTANZA_APERTURE_MS);
    }

    double t0 = ora_s();
    bool mov_avviato = false, mon_avviato = false;
    pthread_t th_mov, th_mon;
    ArgMovimento arg_mov = {c, a_turno};
    ArgMonitor arg_mon = {a, t0, iniziale};
    if (movimento && pthread_create(&th_mov, NULL, a_turno ? thread_movimento_a_turno : thread_movimento_insieme, &arg_mov) == 0)
        mov_avviato = true;
    if (pthread_create(&th_mon, NULL, thread_monitor, &arg_mon) == 0)
        mon_avviato = true;

    while (!atomic_load(&g_sigint) && ora_s() - t0 < secondi) {
        if (!ph_componente_vivo(c)) {
            fprintf(stderr, "il componente sul telefono non risponde piu': misura interrotta\n");
            break;
        }
        for (int n = 0; n < numero; n++) {
            Flusso *x = &g_fl[n];
            if (x->f != NULL && !x->avvisato && atomic_load(&x->vivo) &&
                !atomic_load(&x->primo_ricevuto) && ora_s() - x->t_richiesta > ATTESA_AVVISO_S) {
                x->avvisato = true;
                fprintf(stderr, "flusso %d (%s): nessun fotogramma dopo %.0f s (ultimo evento del telefono: %u)\n",
                        n + 1, x->pacchetto, ATTESA_AVVISO_S, atomic_load(&x->ultimo_evento));
            }
        }
        dormi_ms(100);
    }
    if (atomic_load(&g_sigint))
        fprintf(stderr, "interrotto: chiudo tutto\n");
    atomic_store(&g_fine_misura, ora_s());
    atomic_store(&g_ferma, true);

    if (mov_avviato)
        pthread_join(th_mov, NULL);
    if (mon_avviato)
        pthread_join(th_mon, NULL);
    for (int n = 0; n < numero; n++)
        if (g_fl[n].thread_avviato)
            pthread_join(g_fl[n].thread, NULL);

    StatoTelefono finale = leggi_telefono(a);
    stampa_tabella(atomic_load(&g_fine_misura) - t0, decodifica);
    if (iniziale.ok)
        printf("batteria iniziale %.1f C", iniziale.temperatura);
    else
        printf("batteria iniziale non letta");
    if (finale.ok)
        printf(", finale %.1f C, carico finale (grezzo) %s\n", finale.temperatura,
               finale.carico_ok ? finale.carico : "non leggibile");
    else
        printf(", finale non letta\n");
    uscita = 0;

fine:
    for (int n = 0; n < g_n_fl; n++) {
        if (g_fl[n].f != NULL)
            ph_video_chiudi(g_fl[n].f, true);
        free(g_fl[n].intervalli);
        free(g_fl[n].tel);
        free(g_fl[n].rete);
    }
    if (c != NULL)
        ph_componente_chiudi(c);
    ph_adb_chiudi(a);
    ph_chiave_libera(k);
    return uscita;
}
