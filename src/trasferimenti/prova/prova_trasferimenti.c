/* Prove della coda dei trasferimenti con il telefono finto (finto.c). */
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <ftw.h>
#include <limits.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "finto.h"
#include "trasferimenti/trasferimenti.h"

static int guasti;
#define CONTROLLA(c)                                                                  \
    do {                                                                              \
        if (!(c)) {                                                                   \
            fprintf(stderr, "%s:%d: fallito: %s\n", __FILE__, __LINE__, #c);          \
            guasti++;                                                                 \
        }                                                                             \
    } while (0)

/* ---- Ambiente di prova ------------------------------------------------------ */

static char base[400], tel[512], pc[512];
#define SD "/storage/emulated/0"

static int rimuovi_cb(const char *p, const struct stat *s, int f, struct FTW *w)
{
    (void)s; (void)f; (void)w;
    remove(p);
    return 0;
}

static void ambiente_nuovo(void)
{
    if (base[0])
        nftw(base, rimuovi_cb, 16, FTW_DEPTH | FTW_PHYS);
    const char *t = getenv("TMPDIR");
    snprintf(base, sizeof base, "%s/ph_tr_XXXXXX", t && *t ? t : "/tmp");
    if (!mkdtemp(base)) {
        perror("mkdtemp");
        exit(2);
    }
    snprintf(tel, sizeof tel, "%s/tel", base);
    snprintf(pc, sizeof pc, "%s/pc", base);
    mkdir(tel, 0755);
    mkdir(pc, 0755);
    finto_reset();
    finto_radice(tel);
}

static void ambiente_fine(void)
{
    nftw(base, rimuovi_cb, 16, FTW_DEPTH | FTW_PHYS);
    base[0] = '\0';
}

static void scrivi_file(const char *percorso, size_t n, unsigned seme)
{
    char dir[PATH_MAX];
    snprintf(dir, sizeof dir, "%s", percorso);
    char *s = strrchr(dir, '/');
    if (s) {
        *s = '\0';
        for (char *p = dir + 1; *p; p++)
            if (*p == '/') {
                *p = '\0';
                mkdir(dir, 0755);
                *p = '/';
            }
        mkdir(dir, 0755);
    }
    FILE *f = fopen(percorso, "wb");
    if (!f) {
        perror(percorso);
        exit(2);
    }
    for (size_t i = 0; i < n; i++)
        fputc((int)((i * 31 + seme + (i >> 8)) & 0xff), f);
    fclose(f);
}

static bool esiste(const char *p)
{
    struct stat s;
    return lstat(p, &s) == 0;
}

static long dimensione(const char *p)
{
    struct stat s;
    return stat(p, &s) == 0 ? (long)s.st_size : -1;
}

static bool uguali(const char *a, const char *b)
{
    FILE *fa = fopen(a, "rb"), *fb = fopen(b, "rb");
    bool r = fa && fb;
    while (r) {
        int x = fgetc(fa), y = fgetc(fb);
        if (x != y)
            r = false;
        if (x == EOF)
            break;
    }
    if (fa) fclose(fa);
    if (fb) fclose(fb);
    return r;
}

/* Percorso reale nel telefono finto di un percorso del telefono. */
static const char *tp(const char *telefono)
{
    static __thread char buf[4][PATH_MAX];
    static __thread int k;
    k = (k + 1) % 4;
    finto_percorso_reale(telefono, buf[k], PATH_MAX);
    return buf[k];
}

static const char *pcp(const char *rel)
{
    static __thread char buf[4][PATH_MAX];
    static __thread int k;
    k = (k + 1) % 4;
    snprintf(buf[k], PATH_MAX, "%s/%s", pc, rel);
    return buf[k];
}

static int conta_voci_cartella(const char *dir)
{
    DIR *d = opendir(dir);
    if (!d)
        return 0;
    int n = 0;
    struct dirent *e;
    while ((e = readdir(d)))
        if (strcmp(e->d_name, ".") && strcmp(e->d_name, ".."))
            n++;
    closedir(d);
    return n;
}

static int conta_fd(void)
{
    return conta_voci_cartella("/proc/self/fd");
}

/* ---- Registro degli avvisi ---------------------------------------------------- */

typedef struct {
    uint64_t id;
    PhTrStato stato;
    uint64_t fatti, dimensione;
    uint64_t ms;
} Evento;

#define EV_MAX 200000
static Evento *eventi;
static size_t n_eventi;
static pthread_mutex_t m_ev = PTHREAD_MUTEX_INITIALIZER;
static pthread_t thread_avvisi;
static bool thread_avvisi_noto;
static bool thread_avvisi_vario;
static int avviso_lento_us;

static uint64_t ora_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000 + (uint64_t)t.tv_nsec / 1000000;
}

static bool terminale(PhTrStato s);
/* Cancello dell'avviso di coda: se chiuso, l'avviso (voce NULL) segnala "iniziato"
 * e aspetta (al massimo 10 s) che la prova lo apra, poi conta. */
static pthread_mutex_t m_cancello = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t c_cancello = PTHREAD_COND_INITIALIZER;
static bool cancello_chiuso;
static int avvisi_coda_iniziati;
static atomic_int avvisi_coda;       /* avvisi "la coda si e' fermata" (voce NULL) */
static atomic_int rientro_attivo;    /* l'avviso finale richiama la coda */
static atomic_int rientro_extra;     /* voci accodate dall'avviso */
static atomic_int rientro_chiamate;
static PhTrasferimenti *g_t;
static char rientro_origine[600];

static void avviso(const PhTrVoce *v, void *u)
{
    (void)u;
    if (!v) {
        /* la ferma e' gia' visibile quando arriva l'avviso (il thread di lavoro
         * la scrive prima di avvisare): chi legge ph_tr_coda_ferma puo' vederla
         * PRIMA dell'avviso, mai il contrario */
        struct timespec limite;
        clock_gettime(CLOCK_REALTIME, &limite);
        limite.tv_sec += 10;
        pthread_mutex_lock(&m_cancello);
        avvisi_coda_iniziati++;
        pthread_cond_broadcast(&c_cancello);
        while (cancello_chiuso && pthread_cond_timedwait(&c_cancello, &m_cancello, &limite) == 0)
            ;
        pthread_mutex_unlock(&m_cancello);
        atomic_fetch_add(&avvisi_coda, 1);
        return;
    }
    if (atomic_load(&rientro_attivo) && terminale(v->stato)) {
        PhErrore e1 = {{0}}, e2 = {{0}};
        if (ph_tr_riprendi(g_t, NULL, &e1) == 0 || !e1.testo[0] || ph_tr_svuota(g_t, &e2) == 0 || !e2.testo[0])
            guasti++; /* dall'avviso devono rifiutare, senza bloccarsi */
        /* rientro: dall'avviso finale si puo' usare la coda, anche per la voce stessa */
        PhTrElenco el;
        if (ph_tr_elenca(g_t, &el, NULL) == 0)
            ph_tr_elenco_libera(&el);
        ph_tr_rimuovi_finiti(g_t);
        if (atomic_fetch_add(&rientro_extra, 1) < 3) {
            uint64_t id;
            if (ph_tr_accoda(g_t, PH_TR_INVIO, rientro_origine, SD "/Rientro", &id, NULL) != 0)
                guasti++;
        }
        (void)ph_tr_in_corso(g_t);
        atomic_fetch_add(&rientro_chiamate, 1);
    }
    if (avviso_lento_us)
        usleep((useconds_t)avviso_lento_us);
    pthread_mutex_lock(&m_ev);
    if (!thread_avvisi_noto) {
        thread_avvisi = pthread_self();
        thread_avvisi_noto = true;
    } else if (!pthread_equal(thread_avvisi, pthread_self())) {
        thread_avvisi_vario = true;
    }
    if (n_eventi < EV_MAX) {
        eventi[n_eventi++] = (Evento){v->id, v->stato, v->fatti, v->dimensione, ora_ms()};
    }
    /* le stringhe non sono mai NULL */
    if (!v->nome || !v->origine || !v->destinazione || !v->errore)
        guasti++;
    pthread_mutex_unlock(&m_ev);
}

static void eventi_azzera(void)
{
    pthread_mutex_lock(&m_ev);
    n_eventi = 0;
    thread_avvisi_noto = false;
    pthread_mutex_unlock(&m_ev);
}

static bool terminale(PhTrStato s)
{
    return s == PH_TR_COMPLETATO || s == PH_TR_FALLITO || s == PH_TR_ANNULLATO;
}

/* Quante voci hanno consegnato l'avviso finale. */
static size_t finali_consegnati(void)
{
    size_t n = 0;
    pthread_mutex_lock(&m_ev);
    for (size_t i = 0; i < n_eventi; i++)
        if (terminale(eventi[i].stato))
            n++;
    pthread_mutex_unlock(&m_ev);
    return n;
}

static bool finale_di(uint64_t id, Evento *out)
{
    bool r = false;
    pthread_mutex_lock(&m_ev);
    for (size_t i = 0; i < n_eventi; i++)
        if (eventi[i].id == id && terminale(eventi[i].stato)) {
            if (out)
                *out = eventi[i];
            r = true;
        }
    pthread_mutex_unlock(&m_ev);
    return r;
}

static bool aspetta(bool (*cond)(size_t), size_t arg, int sec)
{
    uint64_t fine = ora_ms() + (uint64_t)sec * 1000;
    while (ora_ms() < fine) {
        if (cond(arg))
            return true;
        usleep(2000);
    }
    return cond(arg);
}

static bool c_finali(size_t n) { return finali_consegnati() >= n; }
static bool c_avvisi_coda(size_t n) { return (size_t)atomic_load(&avvisi_coda) >= n; }
static bool c_avviso_iniziato(size_t n)
{
    pthread_mutex_lock(&m_cancello);
    bool r = (size_t)avvisi_coda_iniziati >= n;
    pthread_mutex_unlock(&m_cancello);
    return r;
}
static void cancello(bool chiuso)
{
    pthread_mutex_lock(&m_cancello);
    cancello_chiuso = chiuso;
    pthread_cond_broadcast(&c_cancello);
    pthread_mutex_unlock(&m_cancello);
}
static bool c_comandi(size_t n) { return finto_comandi_numero() >= n; }
static bool c_ferma(size_t n) { (void)n; return ph_tr_coda_ferma(g_t); }

static PhTrasferimenti *coda_nuova(PhAdb *a)
{
    eventi_azzera();
    atomic_store(&avvisi_coda, 0);
    atomic_store(&rientro_attivo, 0);
    PhErrore e = {{0}};
    PhTrasferimenti *t = ph_tr_crea(a, avviso, NULL, &e);
    CONTROLLA(t != NULL);
    if (!t)
        exit(2);
    g_t = t;
    return t;
}

static uint64_t accoda_ok(PhTrasferimenti *t, PhTrVerso v, const char *orig, const char *cart)
{
    uint64_t id = 0;
    PhErrore e = {{0}};
    int r = ph_tr_accoda(t, v, orig, cart, &id, &e);
    if (r != 0)
        fprintf(stderr, "accoda %s: %s\n", orig, e.testo);
    CONTROLLA(r == 0);
    return id;
}

/* Copia di una voce dallo stato corrente. false se l'id non c'e'. */
static bool voce_di(PhTrasferimenti *t, uint64_t id, PhTrVoce *o)
{
    PhTrElenco el;
    PhErrore e = {{0}};
    if (ph_tr_elenca(t, &el, &e) != 0)
        return false;
    bool r = false;
    for (size_t i = 0; i < el.numero; i++)
        if (el.voci[i].id == id) {
            *o = el.voci[i];
            el.voci[i] = (PhTrVoce){0};
            r = true;
            break;
        }
    for (size_t i = 0; i < el.numero; i++) {
        free(el.voci[i].nome);
        free(el.voci[i].origine);
        free(el.voci[i].destinazione);
        free(el.voci[i].errore);
    }
    free(el.voci);
    return r;
}

static void voce_libera(PhTrVoce *o)
{
    free(o->nome);
    free(o->origine);
    free(o->destinazione);
    free(o->errore);
}

static PhTrStato stato_di(PhTrasferimenti *t, uint64_t id)
{
    PhTrVoce v;
    if (!voce_di(t, id, &v))
        return (PhTrStato)-1;
    PhTrStato s = v.stato;
    voce_libera(&v);
    return s;
}

/* Destinazione della voce (copia in buffer statico per thread). */
static const char *dest_di(PhTrasferimenti *t, uint64_t id)
{
    static __thread char b[8][PATH_MAX];
    static __thread int k;
    k = (k + 1) % 8;
    PhTrVoce v;
    b[k][0] = '\0';
    if (voce_di(t, id, &v)) {
        snprintf(b[k], PATH_MAX, "%s", v.destinazione);
        voce_libera(&v);
    }
    return b[k];
}

static bool stato_raggiunto(PhTrasferimenti *t, uint64_t id, PhTrStato s, int sec)
{
    uint64_t fine = ora_ms() + (uint64_t)sec * 1000;
    while (ora_ms() < fine) {
        if (stato_di(t, id) == s)
            return true;
        usleep(2000);
    }
    return stato_di(t, id) == s;
}

/* ---- Prove: funzioni pure ----------------------------------------------------- */

static void prova_nomi(void)
{
    static const struct { const char *nome; unsigned n; const char *atteso; } caso[] = {
        {"a.txt", 0, "a.txt"},
        {"a.txt", 1, "a (1).txt"},
        {"a.txt", 12, "a (12).txt"},
        {"a.tar.gz", 1, "a (1).tar.gz"},
        {"A.TAR.GZ", 2, "A (2).TAR.GZ"},
        {"x.tar.zst", 3, "x (3).tar.zst"},
        {"x.tar.bz2", 1, "x (1).tar.bz2"},
        {"x.tar", 1, "x (1).tar"},
        {"x.gz", 1, "x (1).gz"},
        {"a.b.c", 1, "a.b (1).c"},
        {"Makefile", 1, "Makefile (1)"},
        {".bashrc", 1, ".bashrc (1)"},
        {".tar.gz", 1, ".tar (1).gz"},
        {"a.", 1, "a. (1)"},
        {"foto 1.5 finale", 1, "foto 1.5 finale (1)"},
        {"a.estensionelunga", 1, "a.estensionelunga (1)"},
        {"relazione.2024.pdf", 2, "relazione.2024 (2).pdf"},
        {"nome (1).txt", 1, "nome (1) (1).txt"},
        {"\xc3\xa8t\xc3\xa0.txt", 1, "\xc3\xa8t\xc3\xa0 (1).txt"},
    };
    for (size_t i = 0; i < sizeof caso / sizeof *caso; i++) {
        char out[300];
        int r = ph_tr_nome_con_indice(caso[i].nome, caso[i].n, out, sizeof out);
        CONTROLLA(r == 0);
        if (r != 0 || strcmp(out, caso[i].atteso) != 0)
            fprintf(stderr, "nome '%s' %u -> '%s' (atteso '%s')\n", caso[i].nome, caso[i].n, out, caso[i].atteso);
        CONTROLLA(r == 0 && strcmp(out, caso[i].atteso) == 0);
    }
    char out[300];
    CONTROLLA(ph_tr_nome_con_indice("", 1, out, sizeof out) != 0);
    CONTROLLA(ph_tr_nome_con_indice("a.txt", 1, out, 5) != 0);
    /* nome lungo: si accorcia il corpo, l'estensione resta e non si spezza UTF-8 */
    char lungo[400];
    memset(lungo, 0, sizeof lungo);
    for (int i = 0; i < 120; i++)
        memcpy(lungo + 2 * i, "\xc3\xa8", 2); /* 240 byte */
    memcpy(lungo + 240, ".tar.gz", 8);        /* 247 byte in tutto */
    CONTROLLA(ph_tr_nome_con_indice(lungo, 1234, out, sizeof out) == 0);
    CONTROLLA(strlen(out) <= 255);
    CONTROLLA(strlen(out) > 7 && strcmp(out + strlen(out) - 7, ".tar.gz") == 0);
    CONTROLLA(strstr(out, " (1234).tar.gz") != NULL);
    /* il corpo prima del numero e' fatto solo di "è" interi */
    size_t corpo = (size_t)(strstr(out, " (1234)") - out);
    CONTROLLA(corpo % 2 == 0);
    for (size_t i = 0; i < corpo; i += 2)
        CONTROLLA((unsigned char)out[i] == 0xc3 && (unsigned char)out[i + 1] == 0xa8);
}

static void prova_percorsi(void)
{
    static const struct { const char *in; const char *atteso; } caso[] = {
        {"/sdcard", SD}, {"/sdcard/", SD}, {"/sdcard/Download", SD "/Download"},
        {"/sdcard/Download//x/", SD "/Download/x"}, {"/sdcardx/a", "/sdcardx/a"},
        {"/", "/"}, {"//a", "/a"}, {"/storage/emulated/0/a b", "/storage/emulated/0/a b"},
        {"/storage//emulated/0/", "/storage/emulated/0"},
    };
    for (size_t i = 0; i < sizeof caso / sizeof *caso; i++) {
        char out[PATH_MAX];
        PhErrore e = {{0}};
        int r = ph_tr_percorso_telefono(caso[i].in, out, sizeof out, &e);
        if (r != 0 || strcmp(out, caso[i].atteso))
            fprintf(stderr, "percorso '%s' -> '%s'\n", caso[i].in, out);
        CONTROLLA(r == 0 && strcmp(out, caso[i].atteso) == 0);
    }
    char out[64];
    PhErrore e = {{0}};
    CONTROLLA(ph_tr_percorso_telefono("relativo", out, sizeof out, &e) != 0 && e.testo[0]);
    CONTROLLA(ph_tr_percorso_telefono("/sdcard/aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", out, sizeof out, &e) != 0);
}

static void prova_scanner(void)
{
    static const struct { const char *percorso; const char *atteso; } caso[] = {
        {SD "/Download/foto.jpg",
         "am broadcast -a android.intent.action.MEDIA_SCANNER_SCAN_FILE -d 'file://" SD "/Download/foto.jpg'"},
        {SD "/a'b; rm -rf x",
         "am broadcast -a android.intent.action.MEDIA_SCANNER_SCAN_FILE -d 'file://" SD "/a%27b%3B%20rm%20-rf%20x'"},
        {SD "/$(reboot)",
         "am broadcast -a android.intent.action.MEDIA_SCANNER_SCAN_FILE -d 'file://" SD "/%24%28reboot%29'"},
        {SD "/a\nb`id`\"c\\d#e?f%g",
         "am broadcast -a android.intent.action.MEDIA_SCANNER_SCAN_FILE -d 'file://" SD
         "/a%0Ab%60id%60%22c%5Cd%23e%3Ff%25g'"},
        {SD "/\xc3\xa8.txt",
         "am broadcast -a android.intent.action.MEDIA_SCANNER_SCAN_FILE -d 'file://" SD "/%C3%A8.txt'"},
    };
    for (size_t i = 0; i < sizeof caso / sizeof *caso; i++) {
        char out[1024];
        int r = ph_tr_comando_scanner(caso[i].percorso, out, sizeof out);
        CONTROLLA(r == 0);
        if (r != 0 || strcmp(out, caso[i].atteso))
            fprintf(stderr, "scanner: '%s'\n  atteso '%s'\n", out, caso[i].atteso);
        CONTROLLA(r == 0 && strcmp(out, caso[i].atteso) == 0);
        /* tra gli apici dell'indirizzo non c'e' nessun apice, ne' a capo */
        const char *ap = strstr(out, "-d '");
        CONTROLLA(ap != NULL);
        if (ap) {
            const char *in = ap + 4;
            size_t l = strlen(in);
            CONTROLLA(l > 0 && in[l - 1] == '\'');
            CONTROLLA(memchr(in, '\'', l - 1) == NULL);
            CONTROLLA(memchr(in, '\n', l) == NULL);
        }
    }
    char piccolo[32];
    CONTROLLA(ph_tr_comando_scanner(SD "/x", piccolo, sizeof piccolo) != 0);
    CONTROLLA(ph_tr_comando_scanner("relativo", piccolo, sizeof piccolo) != 0);
}

/* ---- Prove: coda -------------------------------------------------------------- */

static void prova_sequenza(void)
{
    ambiente_nuovo();
    finto_ritardo_us(1000);
    PhAdb *a = finto_adb_nuovo();
    PhTrasferimenti *t = coda_nuova(a);
    uint64_t id[6];
    char nome[64];
    for (int i = 0; i < 6; i++) {
        snprintf(nome, sizeof nome, "f%d.bin", i);
        scrivi_file(pcp(nome), 60000 + (size_t)i * 7000, (unsigned)i);
        id[i] = accoda_ok(t, PH_TR_INVIO, pcp(nome), "/sdcard/Download/Prove");
    }
    CONTROLLA(ph_tr_in_corso(t));
    CONTROLLA(aspetta(c_finali, 6, 20));
    CONTROLLA(aspetta(c_comandi, 6, 5));
    CONTROLLA(finto_copie_contemporanee_max() == 1);
    /* ordine dei completamenti = ordine di accodamento */
    uint64_t t_prec = 0;
    for (int i = 0; i < 6; i++) {
        Evento ev = {0};
        CONTROLLA(finale_di(id[i], &ev));
        CONTROLLA(ev.stato == PH_TR_COMPLETATO);
        CONTROLLA(ev.fatti == ev.dimensione && ev.dimensione == 60000 + (uint64_t)i * 7000);
        CONTROLLA(ev.ms >= t_prec);
        t_prec = ev.ms;
        snprintf(nome, sizeof nome, "f%d.bin", i);
        char rem[PATH_MAX];
        snprintf(rem, sizeof rem, SD "/Download/Prove/%s", nome);
        CONTROLLA(uguali(pcp(nome), tp(rem)));
        CONTROLLA(strcmp(dest_di(t, id[i]), rem) == 0);
    }
    /* il comando dello scanner e' uno per file, nell'ordine */
    for (int i = 0; i < 6; i++) {
        char *c = finto_comando((size_t)i);
        char rem[PATH_MAX], atteso[PATH_MAX * 3];
        snprintf(rem, sizeof rem, SD "/Download/Prove/f%d.bin", i);
        CONTROLLA(ph_tr_comando_scanner(rem, atteso, sizeof atteso) == 0);
        CONTROLLA(c && strcmp(c, atteso) == 0);
        free(c);
    }
    CONTROLLA(!ph_tr_in_corso(t));
    CONTROLLA(!thread_avvisi_vario);
    pthread_mutex_lock(&m_ev);
    CONTROLLA(thread_avvisi_noto && !pthread_equal(thread_avvisi, pthread_self()));
    pthread_mutex_unlock(&m_ev);
    /* per ogni voce: prima CORSO, poi (al piu' progressi) poi COMPLETATO, fatti mai indietro */
    pthread_mutex_lock(&m_ev);
    for (int i = 0; i < 6; i++) {
        uint64_t prec = 0;
        int fase = 0;
        for (size_t k = 0; k < n_eventi; k++) {
            if (eventi[k].id != id[i])
                continue;
            CONTROLLA(eventi[k].fatti >= prec);
            prec = eventi[k].fatti;
            if (eventi[k].stato == PH_TR_CORSO) {
                CONTROLLA(fase <= 1);
                fase = 1;
            } else if (eventi[k].stato == PH_TR_COMPLETATO) {
                CONTROLLA(fase == 1);
                fase = 2;
            } else {
                CONTROLLA(0);
            }
        }
        CONTROLLA(fase == 2);
    }
    pthread_mutex_unlock(&m_ev);
    ph_tr_rimuovi_finiti(t);
    PhTrElenco el;
    CONTROLLA(ph_tr_elenca(t, &el, NULL) == 0 && el.numero == 0);
    ph_tr_elenco_libera(&el);
    ph_tr_chiudi(t);
    finto_adb_libera(a);
    ambiente_fine();
}

static void prova_nomi_liberi(void)
{
    ambiente_nuovo();
    PhAdb *a = finto_adb_nuovo();
    PhTrasferimenti *t = coda_nuova(a);
    PhErrore e = {{0}};
    uint64_t id;

    /* a) il telefono ha gia' a.txt e a (1).txt */
    scrivi_file(tp(SD "/D/a.txt"), 100, 1);
    scrivi_file(tp(SD "/D/a (1).txt"), 100, 2);
    scrivi_file(pcp("a.txt"), 777, 9);
    id = accoda_ok(t, PH_TR_INVIO, pcp("a.txt"), "/sdcard/D");
    CONTROLLA(aspetta(c_finali, 1, 5));
    CONTROLLA(stato_di(t, id) == PH_TR_COMPLETATO);
    CONTROLLA(strcmp(dest_di(t, id), SD "/D/a (2).txt") == 0);
    CONTROLLA(dimensione(tp(SD "/D/a.txt")) == 100 && dimensione(tp(SD "/D/a (1).txt")) == 100);
    CONTROLLA(uguali(pcp("a.txt"), tp(SD "/D/a (2).txt")));
    {
        PhTrVoce v;
        CONTROLLA(voce_di(t, id, &v) && strcmp(v.nome, "a (2).txt") == 0);
        voce_libera(&v);
    }

    /* b) lenta in testa, poi la stessa origine tre volte: nomi provvisori distinti subito */
    finto_ritardo_us(3000);
    scrivi_file(pcp("big.bin"), 200000, 3);
    scrivi_file(pcp("b.txt"), 500, 4);
    scrivi_file(pcp("x.tar.gz"), 600, 5);
    uint64_t big = accoda_ok(t, PH_TR_INVIO, pcp("big.bin"), SD "/E");
    uint64_t b[3], tg[2];
    for (int i = 0; i < 3; i++)
        b[i] = accoda_ok(t, PH_TR_INVIO, pcp("b.txt"), SD "/E//");
    for (int i = 0; i < 2; i++)
        tg[i] = accoda_ok(t, PH_TR_INVIO, pcp("x.tar.gz"), SD "/E");
    CONTROLLA(strcmp(dest_di(t, b[0]), SD "/E/b.txt") == 0);
    CONTROLLA(strcmp(dest_di(t, b[1]), SD "/E/b (1).txt") == 0);
    CONTROLLA(strcmp(dest_di(t, b[2]), SD "/E/b (2).txt") == 0);
    CONTROLLA(strcmp(dest_di(t, tg[0]), SD "/E/x.tar.gz") == 0);
    CONTROLLA(strcmp(dest_di(t, tg[1]), SD "/E/x (1).tar.gz") == 0);
    CONTROLLA(aspetta(c_finali, 1 + 1 + 3 + 2, 30));
    CONTROLLA(stato_di(t, big) == PH_TR_COMPLETATO);
    for (int i = 0; i < 3; i++)
        CONTROLLA(stato_di(t, b[i]) == PH_TR_COMPLETATO);
    CONTROLLA(uguali(pcp("b.txt"), tp(SD "/E/b.txt")) && uguali(pcp("b.txt"), tp(SD "/E/b (1).txt")) &&
              uguali(pcp("b.txt"), tp(SD "/E/b (2).txt")));
    CONTROLLA(uguali(pcp("x.tar.gz"), tp(SD "/E/x (1).tar.gz")));
    finto_ritardo_us(0);

    /* c) il telefono non distingue maiuscole e minuscole: due voci non prendono "x.txt" e "X.TXT" */
    finto_ritardo_us(3000);
    scrivi_file(pcp("lento.bin"), 100000, 6);
    scrivi_file(pcp("m/x.txt"), 10, 7);
    scrivi_file(pcp("M/X.TXT"), 11, 8);
    size_t prima = finali_consegnati();
    accoda_ok(t, PH_TR_INVIO, pcp("lento.bin"), SD "/F");
    uint64_t m1 = accoda_ok(t, PH_TR_INVIO, pcp("m/x.txt"), SD "/F");
    uint64_t m2 = accoda_ok(t, PH_TR_INVIO, pcp("M/X.TXT"), SD "/F");
    CONTROLLA(strcasecmp(dest_di(t, m1), dest_di(t, m2)) != 0);
    CONTROLLA(aspetta(c_finali, prima + 3, 30));
    CONTROLLA(stato_di(t, m1) == PH_TR_COMPLETATO && stato_di(t, m2) == PH_TR_COMPLETATO);
    finto_ritardo_us(0);

    /* d) ricezione: il PC ha gia' r.txt; due ricezioni della stessa origine */
    scrivi_file(tp(SD "/R/r.txt"), 4000, 10);
    scrivi_file(pcp("r.txt"), 5, 11);
    uint64_t r1 = accoda_ok(t, PH_TR_RICEZIONE, "/sdcard/R/r.txt", pc);
    uint64_t r2 = accoda_ok(t, PH_TR_RICEZIONE, "/sdcard/R/r.txt", pc);
    CONTROLLA(strcmp(dest_di(t, r2), pcp("r (2).txt")) == 0 || strcmp(dest_di(t, r2), pcp("r (1).txt")) == 0);
    CONTROLLA(strcmp(dest_di(t, r1), dest_di(t, r2)) != 0);
    CONTROLLA(aspetta(c_finali, prima + 5, 10));
    CONTROLLA(stato_di(t, r1) == PH_TR_COMPLETATO && stato_di(t, r2) == PH_TR_COMPLETATO);
    CONTROLLA(dimensione(pcp("r.txt")) == 5);
    CONTROLLA(uguali(tp(SD "/R/r.txt"), pcp("r (1).txt")) && uguali(tp(SD "/R/r.txt"), pcp("r (2).txt")));
    CONTROLLA(!esiste(pcp("r (1).txt.parziale")));

    /* e) corsa sul PC: dopo l'accodamento compare r2.txt; alla partenza si sceglie "r2 (1).txt" */
    finto_ritardo_us(3000);
    scrivi_file(pcp("lento2.bin"), 60000, 12);
    scrivi_file(tp(SD "/R/r2.txt"), 300, 13);
    accoda_ok(t, PH_TR_INVIO, pcp("lento2.bin"), SD "/G");
    uint64_t r3 = accoda_ok(t, PH_TR_RICEZIONE, SD "/R/r2.txt", pc);
    CONTROLLA(strcmp(dest_di(t, r3), pcp("r2.txt")) == 0);
    scrivi_file(pcp("r2.txt"), 9, 14);
    CONTROLLA(aspetta(c_finali, prima + 7, 20));
    CONTROLLA(stato_di(t, r3) == PH_TR_COMPLETATO);
    CONTROLLA(strcmp(dest_di(t, r3), pcp("r2 (1).txt")) == 0);
    CONTROLLA(dimensione(pcp("r2.txt")) == 9 && dimensione(pcp("r2 (1).txt")) == 300);
    finto_ritardo_us(0);

    /* f) errori all'accodamento */
    CONTROLLA(ph_tr_accoda(t, PH_TR_INVIO, pcp("non-esiste"), SD, NULL, &e) != 0 && e.testo[0]);
    e.testo[0] = 0;
    CONTROLLA(ph_tr_accoda(t, PH_TR_INVIO, pc, SD, NULL, &e) != 0 && e.testo[0]);
    e.testo[0] = 0;
    CONTROLLA(ph_tr_accoda(t, PH_TR_INVIO, pcp("a.txt"), "relativa", NULL, &e) != 0 && e.testo[0]);
    e.testo[0] = 0;
    CONTROLLA(ph_tr_accoda(t, PH_TR_RICEZIONE, SD "/R/r.txt", pcp("cartella-assente"), NULL, &e) != 0);
    CONTROLLA(strstr(e.testo, "non esiste") != NULL);
    e.testo[0] = 0;
    CONTROLLA(ph_tr_accoda(t, PH_TR_RICEZIONE, "/", pc, NULL, &e) != 0 && e.testo[0]);
    CONTROLLA(ph_tr_accoda(t, PH_TR_RICEZIONE, "senza-barra", pc, NULL, &e) != 0);
    ph_tr_chiudi(t);
    finto_adb_libera(a);
    ambiente_fine();
}

static void prova_percorsi_ostili(void)
{
    ambiente_nuovo();
    PhAdb *a = finto_adb_nuovo();
    PhTrasferimenti *t = coda_nuova(a);
    static const char *const nomi[] = {"a'b; rm -rf x", "$(touch pwned)", "con\nnewline",
                                       "spazi e \xc3\xa0""cc\xc3\xa8nti.txt", "`id`", "q\"d\\e#f?g%h"};
    enum { N = sizeof nomi / sizeof *nomi };
    uint64_t id[N], rx[N];
    for (int i = 0; i < N; i++) {
        scrivi_file(pcp(nomi[i]), 3000 + (size_t)i, (unsigned)i);
        id[i] = accoda_ok(t, PH_TR_INVIO, pcp(nomi[i]), "/sdcard/Cartella con 'apici' e spazi/\xc3\xa8");
    }
    CONTROLLA(aspetta(c_finali, N, 10));
    CONTROLLA(aspetta(c_comandi, N, 5));
    mkdir(pcp("ric"), 0755);
    for (int i = 0; i < N; i++) {
        char rem[PATH_MAX];
        snprintf(rem, sizeof rem, SD "/Cartella con 'apici' e spazi/\xc3\xa8/%s", nomi[i]);
        CONTROLLA(stato_di(t, id[i]) == PH_TR_COMPLETATO);
        CONTROLLA(strcmp(dest_di(t, id[i]), rem) == 0);
        CONTROLLA(uguali(pcp(nomi[i]), tp(rem)));
        char *c = finto_comando((size_t)i);
        char atteso[PATH_MAX * 3];
        CONTROLLA(ph_tr_comando_scanner(rem, atteso, sizeof atteso) == 0);
        CONTROLLA(c && strcmp(c, atteso) == 0);
        /* nessun comando contiene a capo o un apice fuori da quelli dell'indirizzo */
        CONTROLLA(c && strchr(c, '\n') == NULL);
        free(c);
        /* rimando indietro; la data di modifica del telefono deve arrivare sul PC */
        struct timespec ts[2] = {{1500000000 + i, 0}, {1500000000 + i, 0}};
        utimensat(AT_FDCWD, tp(rem), ts, 0);
        char cart[PATH_MAX];
        snprintf(cart, sizeof cart, "%s/ric", pc);
        rx[i] = accoda_ok(t, PH_TR_RICEZIONE, rem, cart);
    }
    CONTROLLA(aspetta(c_finali, 2 * N, 10));
    for (int i = 0; i < N; i++) {
        char dst[PATH_MAX];
        snprintf(dst, sizeof dst, "%s/ric/%s", pc, nomi[i]);
        CONTROLLA(stato_di(t, rx[i]) == PH_TR_COMPLETATO);
        CONTROLLA(strcmp(dest_di(t, rx[i]), dst) == 0);
        CONTROLLA(uguali(pcp(nomi[i]), dst));
        struct stat s;
        CONTROLLA(stat(dst, &s) == 0 && s.st_mtime == 1500000000 + i);
    }
    CONTROLLA(finto_comandi_numero() == N); /* nessuno scanner dopo una ricezione */
    CONTROLLA(!esiste(pcp("pwned")) && !esiste(tp("pwned")));
    ph_tr_chiudi(t);
    finto_adb_libera(a);
    ambiente_fine();
}

static void prova_annullamenti(void)
{
    ambiente_nuovo();
    finto_ritardo_us(2000);
    PhAdb *a = finto_adb_nuovo();
    PhTrasferimenti *t = coda_nuova(a);
    PhErrore e = {{0}};
    scrivi_file(pcp("big.bin"), 2000000, 1);
    scrivi_file(pcp("p.txt"), 100, 2);
    scrivi_file(pcp("q.txt"), 100, 3);

    /* a) in attesa e in corso */
    uint64_t big = accoda_ok(t, PH_TR_INVIO, pcp("big.bin"), SD "/A");
    uint64_t p = accoda_ok(t, PH_TR_INVIO, pcp("p.txt"), SD "/A");
    uint64_t q = accoda_ok(t, PH_TR_INVIO, pcp("q.txt"), SD "/A");
    CONTROLLA(stato_raggiunto(t, big, PH_TR_CORSO, 5));
    usleep(30000);
    CONTROLLA(ph_tr_annulla(t, p, &e) == 0);
    CONTROLLA(stato_di(t, p) == PH_TR_ANNULLATO); /* subito, senza aspettare il thread */
    CONTROLLA(ph_tr_annulla(t, big, &e) == 0);
    CONTROLLA(stato_raggiunto(t, big, PH_TR_ANNULLATO, 5));
    CONTROLLA(aspetta(c_finali, 1, 5)); /* l'annullamento di quella in corso lo dice il thread di lavoro */
    CONTROLLA(stato_raggiunto(t, q, PH_TR_COMPLETATO, 5));
    CONTROLLA(!esiste(tp(SD "/A/big.bin")));
    CONTROLLA(!esiste(tp(SD "/A/p.txt")));
    CONTROLLA(esiste(tp(SD "/A/q.txt")));
    Evento ev;
    CONTROLLA(finale_di(big, &ev) && ev.stato == PH_TR_ANNULLATO);
    CONTROLLA(!finale_di(p, NULL)); /* annullata in attesa: nessun avviso dal thread */
    /* c) gia' terminata o sconosciuta */
    e.testo[0] = 0;
    CONTROLLA(ph_tr_annulla(t, big, &e) != 0 && e.testo[0]);
    CONTROLLA(ph_tr_annulla(t, q, NULL) != 0);
    CONTROLLA(ph_tr_annulla(t, 99999, NULL) != 0);

    /* b) ricezione in corso: spariscono parziale e file vuoto di prenotazione */
    scrivi_file(tp(SD "/B/grande.bin"), 2000000, 4);
    mkdir(pcp("dst"), 0755);
    char dst[PATH_MAX];
    snprintf(dst, sizeof dst, "%s/dst", pc);
    uint64_t r = accoda_ok(t, PH_TR_RICEZIONE, SD "/B/grande.bin", dst);
    CONTROLLA(stato_raggiunto(t, r, PH_TR_CORSO, 5));
    usleep(40000);
    CONTROLLA(conta_voci_cartella(dst) >= 1);
    CONTROLLA(ph_tr_annulla(t, r, NULL) == 0);
    CONTROLLA(stato_raggiunto(t, r, PH_TR_ANNULLATO, 5));
    CONTROLLA(conta_voci_cartella(dst) == 0);

    /* d) annulla tutto: una in corso e dieci in attesa */
    uint64_t ids[11];
    for (int i = 0; i < 11; i++)
        ids[i] = accoda_ok(t, PH_TR_INVIO, pcp("big.bin"), SD "/T");
    CONTROLLA(stato_raggiunto(t, ids[0], PH_TR_CORSO, 5));
    usleep(20000);
    ph_tr_annulla_tutto(t);
    for (int i = 0; i < 11; i++)
        CONTROLLA(stato_raggiunto(t, ids[i], PH_TR_ANNULLATO, 5));
    CONTROLLA(aspetta(c_finali, 3, 5));
    usleep(30000);
    CONTROLLA(conta_voci_cartella(tp(SD "/T")) == 0);
    CONTROLLA(!ph_tr_in_corso(t));

    /* la coda e' ancora viva */
    finto_ritardo_us(0);
    uint64_t dopo = accoda_ok(t, PH_TR_INVIO, pcp("q.txt"), SD "/T");
    CONTROLLA(stato_raggiunto(t, dopo, PH_TR_COMPLETATO, 5));
    ph_tr_chiudi(t);
    finto_adb_libera(a);
    ambiente_fine();
}

typedef struct {
    PhTrasferimenti *t;
    atomic_int fine;
    atomic_ullong massimo;
} Raffica;

static void *raffica_annulla(void *arg)
{
    Raffica *r = arg;
    unsigned seme = 12345;
    int giri = 0;
    while (!atomic_load(&r->fine)) {
        seme = seme * 1103515245u + 12345u;
        uint64_t mx = atomic_load(&r->massimo);
        uint64_t id = 1 + (seme >> 8) % (mx ? mx : 1);
        ph_tr_annulla(r->t, id, NULL);
        if (++giri % 97 == 0)
            ph_tr_annulla_tutto(r->t);
        if (giri % 7 == 0) {
            PhTrElenco el;
            if (ph_tr_elenca(r->t, &el, NULL) == 0)
                ph_tr_elenco_libera(&el);
            (void)ph_tr_in_corso(r->t);
        }
        usleep(100);
    }
    return NULL;
}

static void prova_raffica(void)
{
    ambiente_nuovo();
    finto_ritardo_us(150);
    PhAdb *a = finto_adb_nuovo();
    PhTrasferimenti *t = coda_nuova(a);
    mkdir(pcp("dst"), 0755);
    char dst[PATH_MAX];
    snprintf(dst, sizeof dst, "%s/dst", pc);
    scrivi_file(pcp("s.bin"), 20000, 1);
    scrivi_file(tp(SD "/S/s.bin"), 20000, 2);
    enum { N = 300 };
    Raffica r;
    r.t = t;
    atomic_init(&r.fine, 0);
    atomic_init(&r.massimo, 0);
    pthread_t th;
    pthread_create(&th, NULL, raffica_annulla, &r);
    uint64_t ids[N];
    for (int i = 0; i < N; i++) {
        ids[i] = i % 2 ? accoda_ok(t, PH_TR_INVIO, pcp("s.bin"), SD "/S/out")
                       : accoda_ok(t, PH_TR_RICEZIONE, SD "/S/s.bin", dst);
        atomic_store(&r.massimo, ids[i]);
        if (i % 25 == 0)
            usleep(3000);
    }
    /* la raffica continua finche' non e' tutto terminato */
    for (int sec = 0; sec < 60; sec++) {
        PhTrElenco el;
        ph_tr_elenca(t, &el, NULL);
        size_t vivi = 0;
        for (size_t i = 0; i < el.numero; i++)
            if (!terminale(el.voci[i].stato))
                vivi++;
        ph_tr_elenco_libera(&el);
        if (vivi == 0)
            break;
        usleep(100000);
    }
    atomic_store(&r.fine, 1);
    pthread_join(th, NULL);
    usleep(100000); /* l'ultimo avviso puo' essere ancora in viaggio */
    PhTrElenco el;
    CONTROLLA(ph_tr_elenca(t, &el, NULL) == 0 && el.numero == N);
    int completati = 0, annullati = 0;
    for (size_t i = 0; i < el.numero; i++) {
        PhTrVoce *v = &el.voci[i];
        CONTROLLA(terminale(v->stato));
        bool c = v->stato == PH_TR_COMPLETATO;
        completati += c;
        annullati += v->stato == PH_TR_ANNULLATO;
        CONTROLLA(v->stato != PH_TR_FALLITO);
        if (c)
            CONTROLLA(v->fatti == 20000);
        Evento ev;
        CONTROLLA(finale_di(v->id, &ev) || v->stato == PH_TR_ANNULLATO);
    }
    ph_tr_elenco_libera(&el);
    /* nelle cartelle ci sono tanti file quante voci completate, per verso (le voci annullate
     * prima di partire hanno solo un nome provvisorio, che un'altra puo' aver preso) */
    {
        int inv = 0, ric = 0;
        PhTrElenco e2;
        CONTROLLA(ph_tr_elenca(t, &e2, NULL) == 0);
        for (size_t i = 0; i < e2.numero; i++)
            if (e2.voci[i].stato == PH_TR_COMPLETATO)
                (e2.voci[i].verso == PH_TR_INVIO ? inv++ : ric++);
        ph_tr_elenco_libera(&e2);
        CONTROLLA(conta_voci_cartella(tp(SD "/S/out")) == inv);
        CONTROLLA(conta_voci_cartella(dst) == ric);
    }
    /* niente parziali ne' file vuoti di prenotazione rimasti */
    DIR *d = opendir(dst);
    struct dirent *de;
    int fuori = 0;
    while (d && (de = readdir(d)))
        if (strstr(de->d_name, ".parziale"))
            fuori++;
    if (d)
        closedir(d);
    CONTROLLA(fuori == 0);
    fprintf(stderr, "raffica: %d completate, %d annullate\n", completati, annullati);
    CONTROLLA(annullati > 0);
    ph_tr_chiudi(t);
    finto_adb_libera(a);
    ambiente_fine();
}

static void prova_fallimenti(void)
{
    ambiente_nuovo();
    PhAdb *a = finto_adb_nuovo();
    PhTrasferimenti *t = coda_nuova(a);
    scrivi_file(pcp("ok1.bin"), 30000, 1);
    scrivi_file(pcp("rotto.bin"), 30000, 2);
    scrivi_file(pcp("ok2.bin"), 30000, 3);
    uint64_t i1 = accoda_ok(t, PH_TR_INVIO, pcp("ok1.bin"), SD "/X");
    uint64_t i2 = accoda_ok(t, PH_TR_INVIO, pcp("rotto.bin"), SD "/X");
    uint64_t i3 = accoda_ok(t, PH_TR_INVIO, pcp("ok2.bin"), SD "/X");
    scrivi_file(tp(SD "/Y/rotto.bin"), 30000, 4);
    scrivi_file(tp(SD "/Y/ok.bin"), 30000, 5);
    mkdir(pcp("d"), 0755);
    char dst[PATH_MAX];
    snprintf(dst, sizeof dst, "%s/d", pc);
    uint64_t r1 = accoda_ok(t, PH_TR_RICEZIONE, SD "/Y/rotto.bin", dst);
    uint64_t r2 = accoda_ok(t, PH_TR_RICEZIONE, SD "/Y/non-c-e.bin", dst);
    uint64_t r3 = accoda_ok(t, PH_TR_RICEZIONE, SD "/Y", dst); /* una cartella */
    uint64_t r4 = accoda_ok(t, PH_TR_RICEZIONE, SD "/Y/ok.bin", dst);
    CONTROLLA(aspetta(c_finali, 7, 10));
    CONTROLLA(aspetta(c_comandi, 2, 5));
    CONTROLLA(stato_di(t, i1) == PH_TR_COMPLETATO && stato_di(t, i3) == PH_TR_COMPLETATO);
    CONTROLLA(stato_di(t, r4) == PH_TR_COMPLETATO);
    PhTrStato fall[] = {stato_di(t, i2), stato_di(t, r1), stato_di(t, r2), stato_di(t, r3)};
    for (int i = 0; i < 4; i++)
        CONTROLLA(fall[i] == PH_TR_FALLITO);
    uint64_t idf[] = {i2, r1, r2, r3};
    for (int i = 0; i < 4; i++) {
        PhTrVoce v;
        CONTROLLA(voce_di(t, idf[i], &v) && v.errore[0] != '\0');
        if (i == 3)
            CONTROLLA(strstr(v.errore, "cartella") != NULL);
        CONTROLLA(strstr(v.errore, "Connessione") == NULL);
        voce_libera(&v);
    }
    CONTROLLA(!esiste(tp(SD "/X/rotto.bin")));
    CONTROLLA(esiste(tp(SD "/X/ok1.bin")) && esiste(tp(SD "/X/ok2.bin")));
    /* PC: solo ok.bin, niente parziali ne' file vuoti dei falliti */
    CONTROLLA(conta_voci_cartella(dst) == 1 && esiste(pcp("d/ok.bin")));
    CONTROLLA(!ph_tr_coda_ferma(t));
    CONTROLLA(finto_comandi_numero() == 2); /* nessuno scanner per quelli falliti */

    /* scanner che fallisce: l'invio resta completato */
    finto_shell_fallisce(1);
    uint64_t i4 = accoda_ok(t, PH_TR_INVIO, pcp("ok1.bin"), SD "/X");
    CONTROLLA(stato_raggiunto(t, i4, PH_TR_COMPLETATO, 5));
    CONTROLLA(finale_di(i4, NULL) || aspetta(c_finali, 8, 5));
    CONTROLLA(!ph_tr_coda_ferma(t));
    /* destinazione sul PC che sparisce prima della partenza */
    finto_ritardo_us(2000);
    scrivi_file(pcp("lento.bin"), 100000, 1);
    accoda_ok(t, PH_TR_INVIO, pcp("lento.bin"), SD "/X");
    mkdir(pcp("e"), 0755);
    char dst2[PATH_MAX];
    snprintf(dst2, sizeof dst2, "%s/e", pc);
    uint64_t r5 = accoda_ok(t, PH_TR_RICEZIONE, SD "/Y/ok.bin", dst2);
    rmdir(dst2);
    CONTROLLA(stato_raggiunto(t, r5, PH_TR_FALLITO, 10));
    PhTrVoce v;
    CONTROLLA(voce_di(t, r5, &v) && strstr(v.errore, "non esiste") != NULL);
    voce_libera(&v);
    ph_tr_chiudi(t);
    finto_adb_libera(a);
    ambiente_fine();
}

static void prova_caduta(void)
{
    ambiente_nuovo();
    PhAdb *a1 = finto_adb_nuovo(), *a2 = finto_adb_nuovo();
    PhTrasferimenti *t = coda_nuova(a1);
    scrivi_file(pcp("muori.bin"), 40000, 1);
    scrivi_file(pcp("b.txt"), 3000, 2);
    scrivi_file(pcp("c.txt"), 3000, 3);
    uint64_t m = accoda_ok(t, PH_TR_INVIO, pcp("muori.bin"), SD "/K");
    uint64_t b = accoda_ok(t, PH_TR_INVIO, pcp("b.txt"), SD "/K");
    uint64_t c = accoda_ok(t, PH_TR_INVIO, pcp("c.txt"), SD "/K");
    CONTROLLA(aspetta(c_ferma, 0, 10));
    CONTROLLA(aspetta(c_finali, 1, 5));
    CONTROLLA(stato_di(t, m) == PH_TR_FALLITO);
    PhTrVoce v;
    CONTROLLA(voce_di(t, m, &v) && strncmp(v.errore, "Connessione con il telefono persa", 33) == 0);
    voce_libera(&v);
    CONTROLLA(!esiste(tp(SD "/K/muori.bin")));
    /* l'avviso di coda e' l'ultima cosa che il thread fa prima di fermarsi: lo si
     * aspetta (segnale con scadenza) invece di una pausa. Che dopo non parta
     * nulla e' un'assenza: qui si controlla lo stato subito dopo, non si puo'
     * aspettare un "niente" (limite dichiarato). */
    CONTROLLA(aspetta(c_avvisi_coda, 1, 10));
    CONTROLLA(stato_di(t, b) == PH_TR_ATTESA && stato_di(t, c) == PH_TR_ATTESA);
    CONTROLLA(ph_tr_coda_ferma(t) && !ph_tr_in_corso(t));
    CONTROLLA(atomic_load(&avvisi_coda) == 1); /* uno solo */
    ph_tr_riprendi(t, a2, NULL);
    finto_adb_libera(a1); /* subito: la coda non deve piu' toccarla */
    a1 = NULL;
    CONTROLLA(stato_raggiunto(t, b, PH_TR_COMPLETATO, 5) && stato_raggiunto(t, c, PH_TR_COMPLETATO, 5));
    CONTROLLA(!ph_tr_coda_ferma(t));
    CONTROLLA(uguali(pcp("b.txt"), tp(SD "/K/b.txt")) && uguali(pcp("c.txt"), tp(SD "/K/c.txt")));
    CONTROLLA(aspetta(c_finali, 3, 5));

    /* seconda caduta, poi svuota */
    uint64_t m2 = accoda_ok(t, PH_TR_INVIO, pcp("muori.bin"), SD "/K");
    uint64_t d1 = accoda_ok(t, PH_TR_INVIO, pcp("b.txt"), SD "/K");
    CONTROLLA(aspetta(c_ferma, 0, 10));
    CONTROLLA(stato_di(t, m2) == PH_TR_FALLITO && stato_di(t, d1) == PH_TR_ATTESA);
    ph_tr_svuota(t, NULL);
    finto_adb_libera(a2); /* la coda l'ha staccata: si puo' liberare subito */
    a2 = NULL;
    PhTrElenco el;
    CONTROLLA(ph_tr_elenca(t, &el, NULL) == 0 && el.numero == 0);
    ph_tr_elenco_libera(&el);
    CONTROLLA(ph_tr_coda_ferma(t)); /* ferma e senza connessione finche' non si riprende */
    /* accodando senza connessione la coda resta ferma e non tocca nulla */
    uint64_t d2 = accoda_ok(t, PH_TR_INVIO, pcp("c.txt"), SD "/K2");
    usleep(50000);
    CONTROLLA(ph_tr_coda_ferma(t));
    CONTROLLA(stato_di(t, d2) == PH_TR_ATTESA);
    PhAdb *a4 = finto_adb_nuovo();
    ph_tr_riprendi(t, a4, NULL);
    CONTROLLA(stato_raggiunto(t, d2, PH_TR_COMPLETATO, 5));
    ph_tr_chiudi(t);

    /* connessione gia' morta prima di accodare */
    PhAdb *a3 = finto_adb_nuovo();
    finto_adb_uccidi(a3);
    t = coda_nuova(a3);
    uint64_t z = accoda_ok(t, PH_TR_INVIO, pcp("b.txt"), SD "/Z");
    CONTROLLA(aspetta(c_ferma, 0, 10));
    CONTROLLA(stato_di(t, z) == PH_TR_ATTESA && !ph_tr_in_corso(t));
    CONTROLLA(aspetta(c_avvisi_coda, 1, 10));
    ph_tr_chiudi(t); /* chiudere una coda ferma non si blocca */
    CONTROLLA(atomic_load(&avvisi_coda) == 1); /* uno solo, anche dopo la chiusura */
    finto_adb_libera(a3);
    finto_adb_libera(a4);
    ambiente_fine();
}

static void *chiudi_in_ritardo(void *arg)
{
    usleep(20000);
    ph_tr_chiudi(arg);
    return NULL;
}

static void prova_chiusura(void)
{
    ambiente_nuovo();
    PhAdb *a = finto_adb_nuovo();
    scrivi_file(pcp("big.bin"), 3000000, 1);
    int fd_prima = conta_fd();
    for (int giro = 0; giro < 30; giro++) {
        finto_ritardo_us(500);
        avviso_lento_us = giro % 3 == 0 ? 2000 : 0; /* avvisi che durano */
        if (giro == 0)
            scrivi_file(tp(SD "/C/da-ricevere.bin"), 3000000, 2);
        PhTrasferimenti *t = coda_nuova(a);
        for (int i = 0; i < 5; i++)
            accoda_ok(t, i % 2 ? PH_TR_INVIO : PH_TR_RICEZIONE, i % 2 ? pcp("big.bin") : SD "/C/da-ricevere.bin",
                      i % 2 ? SD "/C" : pc);
        usleep((useconds_t)(giro * 1500));
        if (giro % 2)
            ph_tr_chiudi(t);
        else {
            /* chiusura da un altro thread mentre arrivano avvisi */
            pthread_t th;
            pthread_create(&th, NULL, chiudi_in_ritardo, t);
            for (int k = 0; k < 20; k++) {
                (void)ph_tr_in_corso(t);
                usleep(500);
            }
            pthread_join(th, NULL);
        }
        size_t n = n_eventi;
        usleep(20000);
        CONTROLLA(n == n_eventi); /* dopo la chiusura nessun avviso */
        CONTROLLA(conta_voci_cartella(pc) == 1); /* solo big.bin: niente ricevuto ne' parziale */
    }
    avviso_lento_us = 0;
    /* l'unico file rimasto sul telefono e' quello di partenza; sul PC solo big.bin */
    DIR *d = opendir(pc);
    struct dirent *de;
    int parziali = 0;
    while (d && (de = readdir(d)))
        if (strstr(de->d_name, ".parziale") || strstr(de->d_name, "da-ricevere"))
            parziali++;
    if (d)
        closedir(d);
    CONTROLLA(parziali == 0);
    CONTROLLA(!esiste(tp(SD "/C/big.bin")));
    CONTROLLA(conta_fd() == fd_prima);
    /* chiusura subito dopo la creazione e chiusura di una coda vuota */
    PhTrasferimenti *t = coda_nuova(a);
    ph_tr_chiudi(t);
    ph_tr_chiudi(NULL);
    CONTROLLA(conta_fd() == fd_prima);
    finto_adb_libera(a);
    ambiente_fine();
}

static void prova_avvisi_limitati(void)
{
    ambiente_nuovo();
    finto_ritardo_us(400);
    PhAdb *a = finto_adb_nuovo();
    PhTrasferimenti *t = coda_nuova(a);
    enum { DIM = 2 * 1024 * 1024 }; /* 512 blocchi */
    scrivi_file(pcp("g.bin"), DIM, 1);
    uint64_t t0 = ora_ms();
    uint64_t id = accoda_ok(t, PH_TR_INVIO, pcp("g.bin"), SD "/V");
    CONTROLLA(aspetta(c_finali, 1, 30));
    uint64_t durata = ora_ms() - t0;
    size_t n = 0;
    uint64_t prec = 0;
    Evento ultimo = {0};
    pthread_mutex_lock(&m_ev);
    for (size_t i = 0; i < n_eventi; i++) {
        if (eventi[i].id != id)
            continue;
        n++;
        CONTROLLA(eventi[i].fatti >= prec);
        prec = eventi[i].fatti;
        ultimo = eventi[i];
    }
    pthread_mutex_unlock(&m_ev);
    fprintf(stderr, "avvisi: %zu in %llu ms\n", n, (unsigned long long)durata);
    CONTROLLA(n >= 3);
    CONTROLLA(n <= durata / 100 + 3); /* al massimo ~10 al secondo (piu' CORSO e finale) */
    CONTROLLA(ultimo.stato == PH_TR_COMPLETATO && ultimo.fatti == DIM && ultimo.dimensione == DIM);
    ph_tr_chiudi(t);
    finto_adb_libera(a);
    ambiente_fine();
}


/* Il ".parziale" dell'utente (file o collegamento) non si tocca mai. */
static void prova_parziale(void)
{
    ambiente_nuovo();
    PhAdb *a = finto_adb_nuovo();
    PhTrasferimenti *t = coda_nuova(a);
    scrivi_file(tp(SD "/P/x.txt"), 1000, 1);
    scrivi_file(tp(SD "/P/y.txt"), 1000, 2);
    scrivi_file(tp(SD "/P/corsa.bin"), 5000, 3);
    scrivi_file(pcp("x.txt.parziale"), 7, 4);
    scrivi_file(pcp("bersaglio"), 9, 5);
    CONTROLLA(symlink(pcp("bersaglio"), pcp("y.txt.parziale")) == 0);
    uint64_t x = accoda_ok(t, PH_TR_RICEZIONE, SD "/P/x.txt", pc);
    uint64_t y = accoda_ok(t, PH_TR_RICEZIONE, SD "/P/y.txt", pc);
    /* il nome provvisorio e' gia' diverso: x.txt e y.txt hanno un .parziale */
    CONTROLLA(strcmp(dest_di(t, x), pcp("x (1).txt")) == 0);
    CONTROLLA(strcmp(dest_di(t, y), pcp("y (1).txt")) == 0);
    CONTROLLA(aspetta(c_finali, 2, 10));
    CONTROLLA(stato_di(t, x) == PH_TR_COMPLETATO && stato_di(t, y) == PH_TR_COMPLETATO);
    CONTROLLA(dimensione(pcp("x.txt.parziale")) == 7);
    CONTROLLA(dimensione(pcp("bersaglio")) == 9);
    struct stat st;
    CONTROLLA(lstat(pcp("y.txt.parziale"), &st) == 0 && S_ISLNK(st.st_mode));
    CONTROLLA(!esiste(pcp("x.txt")) && !esiste(pcp("y.txt")));
    CONTROLLA(uguali(tp(SD "/P/x.txt"), pcp("x (1).txt")) && uguali(tp(SD "/P/y.txt"), pcp("y (1).txt")));

    /* il .parziale compare dopo la scelta: la ricezione fallisce, si passa al numero dopo */
    finto_corsa_parziale(1);
    uint64_t c = accoda_ok(t, PH_TR_RICEZIONE, SD "/P/corsa.bin", pc);
    CONTROLLA(stato_raggiunto(t, c, PH_TR_COMPLETATO, 10));
    CONTROLLA(strcmp(dest_di(t, c), pcp("corsa (1).bin")) == 0);
    CONTROLLA(uguali(tp(SD "/P/corsa.bin"), pcp("corsa (1).bin")));
    CONTROLLA(dimensione(pcp("corsa.bin.parziale")) == 6); /* "ALTRUI" intatto */
    CONTROLLA(!esiste(pcp("corsa.bin"))); /* la prenotazione e' stata tolta */

    /* una prenotazione sostituita da un file dell'utente durante la ricezione */
    scrivi_file(tp(SD "/P/sosti.bin"), 3000, 6);
    finto_sostituisci_prenotazione(1);
    uint64_t so = accoda_ok(t, PH_TR_RICEZIONE, SD "/P/sosti.bin", pc);
    CONTROLLA(stato_raggiunto(t, so, PH_TR_COMPLETATO, 10));
    CONTROLLA(strcmp(dest_di(t, so), pcp("sosti (1).bin")) == 0);
    CONTROLLA(uguali(tp(SD "/P/sosti.bin"), pcp("sosti (1).bin")));
    CONTROLLA(dimensione(pcp("sosti.bin")) == 6); /* "UTENTE" intatto */
    CONTROLLA(!esiste(pcp("sosti.bin.parziale")) && !esiste(pcp("sosti (1).bin.parziale")));

    /* limite del nome in ricezione: 255 - 9 byte */
    PhErrore e = {{0}};
    char lungo[400];
    strcpy(lungo, SD "/P/");
    size_t l = strlen(lungo);
    memset(lungo + l, 'n', 247);
    lungo[l + 247] = '\0';
    CONTROLLA(ph_tr_accoda(t, PH_TR_RICEZIONE, lungo, pc, NULL, &e) != 0 && e.testo[0]);
    lungo[l + 246] = '\0';
    CONTROLLA(ph_tr_accoda(t, PH_TR_RICEZIONE, lungo, pc, NULL, NULL) == 0);
    ph_tr_chiudi(t);
    finto_adb_libera(a);
    ambiente_fine();
}

/* La PhAdb si libera subito dopo riprendi/svuota/stacca, anche con scanner lento. */
static void prova_vita_adb(void)
{
    ambiente_nuovo();
    scrivi_file(pcp("a.txt"), 2000, 1);
    scrivi_file(pcp("muori.bin"), 40000, 2);

    /* a) riprendi mentre lo scanner e' ancora in corso con la vecchia connessione */
    PhAdb *a1 = finto_adb_nuovo();
    PhTrasferimenti *t = coda_nuova(a1);
    finto_shell_ritardo_ms(300);
    uint64_t i1 = accoda_ok(t, PH_TR_INVIO, pcp("a.txt"), SD "/V");
    CONTROLLA(stato_raggiunto(t, i1, PH_TR_COMPLETATO, 5)); /* completato prima dello scanner */
    PhAdb *a2 = finto_adb_nuovo();
    ph_tr_riprendi(t, a2, NULL);        /* aspetta la fine dello scanner */
    finto_adb_libera(a1);         /* ASan zitto: nessun uso dopo il ritorno */
    CONTROLLA(finto_comandi_numero() == 1);
    finto_shell_ritardo_ms(0);
    uint64_t i2 = accoda_ok(t, PH_TR_INVIO, pcp("a.txt"), SD "/V");
    CONTROLLA(stato_raggiunto(t, i2, PH_TR_COMPLETATO, 5));
    ph_tr_chiudi(t);
    finto_adb_libera(a2);
    CONTROLLA(finto_comandi_numero() >= 1);

    /* b) caduta con la voce in corso, poi svuota e liberazione subito */
    a1 = finto_adb_nuovo();
    t = coda_nuova(a1);
    uint64_t m = accoda_ok(t, PH_TR_INVIO, pcp("muori.bin"), SD "/V");
    uint64_t w = accoda_ok(t, PH_TR_INVIO, pcp("a.txt"), SD "/V");
    CONTROLLA(aspetta(c_ferma, 0, 10));
    CONTROLLA(stato_di(t, m) == PH_TR_FALLITO && stato_di(t, w) == PH_TR_ATTESA);
    ph_tr_svuota(t, NULL);
    finto_adb_libera(a1);
    usleep(50000);
    CONTROLLA(ph_tr_coda_ferma(t));
    a2 = finto_adb_nuovo();
    uint64_t w2 = accoda_ok(t, PH_TR_INVIO, pcp("a.txt"), SD "/V");
    usleep(30000);
    CONTROLLA(stato_di(t, w2) == PH_TR_ATTESA);
    ph_tr_riprendi(t, a2, NULL);
    CONTROLLA(stato_raggiunto(t, w2, PH_TR_COMPLETATO, 5));

    /* c) la connessione cade a coda inattiva: si stacca e si libera */
    finto_adb_uccidi(a2);
    ph_tr_riprendi(t, NULL, NULL);
    finto_adb_libera(a2);
    CONTROLLA(ph_tr_coda_ferma(t));
    uint64_t w3 = accoda_ok(t, PH_TR_INVIO, pcp("a.txt"), SD "/V");
    usleep(30000);
    CONTROLLA(stato_di(t, w3) == PH_TR_ATTESA);
    PhAdb *a3 = finto_adb_nuovo();
    ph_tr_riprendi(t, a3, NULL);
    CONTROLLA(stato_raggiunto(t, w3, PH_TR_COMPLETATO, 5));
    ph_tr_chiudi(t);
    finto_adb_libera(a3);
    ambiente_fine();
}

/* Intreccio forzato: l'avviso di coda ferma e' lento. La ferma e' leggibile
 * (ph_tr_coda_ferma) prima che l'avviso sia arrivato: e' l'ordine del codice,
 * e il controllo vecchio (contare l'avviso subito dopo aver visto la ferma)
 * falliva proprio cosi', sotto carico. */
static void prova_ferma_prima_dell_avviso(void)
{
    ambiente_nuovo();
    scrivi_file(pcp("b.txt"), 100, 1);
    PhAdb *a = finto_adb_nuovo();
    finto_adb_uccidi(a);
    pthread_mutex_lock(&m_cancello);
    avvisi_coda_iniziati = 0;
    pthread_mutex_unlock(&m_cancello);
    cancello(true);
    PhTrasferimenti *t = coda_nuova(a);
    accoda_ok(t, PH_TR_INVIO, pcp("b.txt"), SD "/Z");
    CONTROLLA(aspetta(c_ferma, 0, 10));
    CONTROLLA(aspetta(c_avviso_iniziato, 1, 10)); /* l'avviso e' iniziato ma tenuto fermo dal cancello */
    CONTROLLA(ph_tr_coda_ferma(t));               /* la ferma e' gia' visibile... */
    CONTROLLA(atomic_load(&avvisi_coda) == 0);    /* ... prima che l'avviso sia finito */
    cancello(false);
    CONTROLLA(aspetta(c_avvisi_coda, 1, 10));     /* ... e poi arriva, una volta sola */
    ph_tr_chiudi(t);
    CONTROLLA(atomic_load(&avvisi_coda) == 1);
    finto_adb_libera(a);
    ambiente_fine();
}

/* Dall'avviso finale si puo' usare la coda (anche togliere la voce stessa). */
static void prova_rientro(void)
{
    ambiente_nuovo();
    PhAdb *a = finto_adb_nuovo();
    scrivi_file(pcp("r.txt"), 1500, 1);
    snprintf(rientro_origine, sizeof rientro_origine, "%s", pcp("r.txt"));
    PhTrasferimenti *t = coda_nuova(a);
    atomic_store(&rientro_extra, 0);
    atomic_store(&rientro_chiamate, 0);
    atomic_store(&rientro_attivo, 1);
    for (int i = 0; i < 4; i++)
        accoda_ok(t, PH_TR_INVIO, rientro_origine, SD "/Rientro");
    /* 4 voci + 3 accodate dall'avviso */
    CONTROLLA(aspetta(c_finali, 7, 10));
    usleep(50000);
    CONTROLLA(atomic_load(&rientro_chiamate) == 7);
    CONTROLLA(conta_voci_cartella(tp(SD "/Rientro")) == 7);
    atomic_store(&rientro_attivo, 0);
    ph_tr_chiudi(t);
    finto_adb_libera(a);
    ambiente_fine();
}

int main(void)
{
    eventi = calloc(EV_MAX, sizeof *eventi);
    if (!eventi)
        return 2;
    prova_nomi();
    prova_percorsi();
    prova_scanner();
    prova_sequenza();
    prova_nomi_liberi();
    prova_percorsi_ostili();
    prova_annullamenti();
    prova_raffica();
    prova_fallimenti();
    prova_parziale();
    prova_vita_adb();
    prova_rientro();
    prova_ferma_prima_dell_avviso();
    prova_caduta();
    prova_chiusura();
    prova_avvisi_limitati();
    free(eventi);
    if (guasti)
        fprintf(stderr, "%d controlli falliti\n", guasti);
    else
        printf("trasferimenti: tutto ok\n");
    return guasti ? 1 : 0;
}
