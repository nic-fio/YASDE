/* Prove di src/file contro il telefono finto (finto_canale.c). */
#include "file/file.h"

#include <errno.h>
#include <fcntl.h>
#include <ftw.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "finto_canale.h"

#define CHECK(c)                                                                     \
    do {                                                                             \
        if (!(c)) {                                                                  \
            fprintf(stderr, "%s:%d: fallito: %s\n", __FILE__, __LINE__, #c);         \
            exit(1);                                                                 \
        }                                                                            \
    } while (0)

static PhAdb *tel;
static char loc[512]; /* cartella locale temporanea */

static void percorso_loc(char *out, size_t n, const char *nome)
{
    snprintf(out, n, "%s/%s", loc, nome);
}

static void crea_dir_tel(const char *p)
{
    char r[1200];
    finto_percorso(tel, p, r, sizeof r);
    char *s = strchr(r + strlen(tel->radice) + 1, '/');
    while (s) {
        *s = '\0';
        mkdir(r, 0755);
        *s = '/';
        s = strchr(s + 1, '/');
    }
    mkdir(r, 0755);
}

/* Contenuto pseudo-casuale riproducibile. */
static void riempi(const char *path, size_t n, uint64_t seme, time_t mtime)
{
    FILE *f = fopen(path, "wb");
    CHECK(f);
    uint64_t x = seme * 2654435761u + 88172645463325252ull;
    for (size_t i = 0; i < n; i++) {
        x ^= x << 13;
        x ^= x >> 7;
        x ^= x << 17;
        CHECK(fputc((int)(x & 0xFF), f) != EOF);
    }
    CHECK(fclose(f) == 0);
    struct timespec t[2] = {{mtime, 0}, {mtime, 0}};
    CHECK(utimensat(AT_FDCWD, path, t, 0) == 0);
}

static bool esiste(const char *p)
{
    struct stat sb;
    return lstat(p, &sb) == 0;
}

static bool uguali(const char *a, const char *b)
{
    FILE *fa = fopen(a, "rb"), *fb = fopen(b, "rb");
    if (!fa || !fb) {
        if (fa)
            fclose(fa);
        if (fb)
            fclose(fb);
        return false;
    }
    bool ok = true;
    for (;;) {
        unsigned char x[4096], y[4096];
        size_t nx = fread(x, 1, sizeof x, fa), ny = fread(y, 1, sizeof y, fb);
        if (nx != ny || memcmp(x, y, nx) != 0) {
            ok = false;
            break;
        }
        if (nx == 0)
            break;
    }
    fclose(fa);
    fclose(fb);
    return ok;
}

static time_t mtime_di(const char *p)
{
    struct stat sb;
    CHECK(stat(p, &sb) == 0);
    return sb.st_mtime;
}

typedef struct {
    uint64_t ultimo, totale;
    int chiamate;
    bool monotono;
    atomic_bool *annulla;
    uint64_t soglia; /* se != 0 e fatti >= soglia: annulla */
} Prog;

static void progresso(uint64_t fatti, uint64_t totale, void *u)
{
    Prog *p = u;
    if (fatti < p->ultimo)
        p->monotono = false;
    p->ultimo = fatti;
    p->totale = totale;
    p->chiamate++;
    if (p->soglia && fatti >= p->soglia && p->annulla)
        atomic_store(p->annulla, true);
}

static void prog_init(Prog *p)
{
    memset(p, 0, sizeof *p);
    p->monotono = true;
}

static char buf_loc[600], buf_loc2[600], buf_rem[600];

/* ---- Prove ---------------------------------------------------------------- */

static void prova_andata_e_ritorno(void)
{
    static const size_t dimensioni[] = {0, 1, 65535, 65536, 65537, 1048575, 1048576, 1048577,
                                        3u * 1024 * 1024 + 123, 5u * 1024 * 1024 + 4321};
    crea_dir_tel("/sdcard/Download");
    for (size_t i = 0; i < sizeof dimensioni / sizeof *dimensioni; i++) {
        size_t n = dimensioni[i];
        time_t mt = 1700000000 + (time_t)i * 1000;
        percorso_loc(buf_loc, sizeof buf_loc, "origine.bin");
        riempi(buf_loc, n, i + 1, mt);
        /* Nome con spazi, accento e virgola: la virgola e' anche il separatore di SEND. */
        const char *remoto = "/sdcard/Download/file con spazi, virgola e \xc3\xa8.bin";
        PhErrore e = {{0}};
        Prog p;
        prog_init(&p);
        CHECK(ph_file_invia(tel, buf_loc, remoto, progresso, &p, NULL, &e) == PH_FILE_RIUSCITO);
        CHECK(p.monotono && p.chiamate >= 1 && p.ultimo == n && p.totale == n);
        finto_percorso(tel, remoto, buf_rem, sizeof buf_rem);
        CHECK(uguali(buf_loc, buf_rem));
        CHECK(mtime_di(buf_rem) == mt);

        percorso_loc(buf_loc2, sizeof buf_loc2, "ritorno.bin");
        prog_init(&p);
        CHECK(ph_file_ricevi(tel, remoto, buf_loc2, progresso, &p, NULL, &e) == PH_FILE_RIUSCITO);
        CHECK(p.monotono && p.ultimo == n && p.totale == n);
        CHECK(uguali(buf_loc, buf_loc2));
        CHECK(mtime_di(buf_loc2) == mt);
        char parz[700];
        snprintf(parz, sizeof parz, "%s.parziale", buf_loc2);
        CHECK(!esiste(parz));
        unlink(buf_loc2);
        unlink(buf_rem);
    }
    /* Sovrascrittura di un file piu' lungo con uno piu' corto, e e == NULL. */
    percorso_loc(buf_loc, sizeof buf_loc, "o1.bin");
    riempi(buf_loc, 100000, 7, 1700000000);
    CHECK(ph_file_invia(tel, buf_loc, "/sdcard/Download/s.bin", NULL, NULL, NULL, NULL) == PH_FILE_RIUSCITO);
    riempi(buf_loc, 10, 8, 1700000001);
    CHECK(ph_file_invia(tel, buf_loc, "/sdcard/Download/s.bin", NULL, NULL, NULL, NULL) == PH_FILE_RIUSCITO);
    finto_percorso(tel, "/sdcard/Download/s.bin", buf_rem, sizeof buf_rem);
    CHECK(uguali(buf_loc, buf_rem));
    unlink(buf_rem);
}

static void prova_annullamento(void)
{
    percorso_loc(buf_loc, sizeof buf_loc, "grande.bin");
    riempi(buf_loc, 3u * 1024 * 1024, 5, 1700000000);
    PhErrore e = {{0}};
    atomic_bool annulla = false;
    Prog p;
    prog_init(&p);
    p.annulla = &annulla;
    p.soglia = 200000;
    unsigned rm0 = tel->rm_eseguiti;
    CHECK(ph_file_invia(tel, buf_loc, "/sdcard/Download/annullato.bin", progresso, &p, &annulla, &e) ==
          PH_FILE_ANNULLATO);
    CHECK(p.ultimo < 3u * 1024 * 1024);
    finto_percorso(tel, "/sdcard/Download/annullato.bin", buf_rem, sizeof buf_rem);
    CHECK(!esiste(buf_rem));
    CHECK(tel->rm_eseguiti == rm0 + 1);

    /* Gia' annullato prima di partire. */
    atomic_store(&annulla, true);
    CHECK(ph_file_invia(tel, buf_loc, "/sdcard/Download/annullato.bin", NULL, NULL, &annulla, &e) ==
          PH_FILE_ANNULLATO);
    CHECK(!esiste(buf_rem));

    /* Ricezione annullata a meta': ne' parziale ne' file. */
    atomic_store(&annulla, false);
    finto_percorso(tel, "/sdcard/Download/grande.bin", buf_rem, sizeof buf_rem);
    {
        FILE *f = fopen(buf_rem, "wb");
        CHECK(f);
        fclose(f);
    }
    CHECK(ph_file_invia(tel, buf_loc, "/sdcard/Download/grande.bin", NULL, NULL, NULL, &e) == PH_FILE_RIUSCITO);
    percorso_loc(buf_loc2, sizeof buf_loc2, "scaricato.bin");
    prog_init(&p);
    p.annulla = &annulla;
    p.soglia = 300000;
    CHECK(ph_file_ricevi(tel, "/sdcard/Download/grande.bin", buf_loc2, progresso, &p, &annulla, &e) ==
          PH_FILE_ANNULLATO);
    char parz[700];
    snprintf(parz, sizeof parz, "%s.parziale", buf_loc2);
    CHECK(!esiste(parz) && !esiste(buf_loc2));
    unlink(buf_rem);
}

static void prova_errori_invio(void)
{
    PhErrore e = {{0}};
    percorso_loc(buf_loc, sizeof buf_loc, "piccolo.bin");
    riempi(buf_loc, 200000, 3, 1700000000);

    /* FAIL del telefono a fine invio: errore generico, parziale tolto. */
    tel->o.fail_invio = true;
    CHECK(ph_file_invia(tel, buf_loc, "/sdcard/Download/pieno.bin", NULL, NULL, NULL, &e) == PH_FILE_ERRORE);
    CHECK(strstr(e.testo, "No space left") != NULL);
    finto_percorso(tel, "/sdcard/Download/pieno.bin", buf_rem, sizeof buf_rem);
    CHECK(!esiste(buf_rem));
    finto_opzioni_normali(tel);

    /* Accesso negato: e un file vecchio con quel nome resta intatto. */
    crea_dir_tel("/sdcard/negato");
    finto_percorso(tel, "/sdcard/negato/vecchio.bin", buf_rem, sizeof buf_rem);
    riempi(buf_rem, 50, 9, 1600000000);
    CHECK(ph_file_invia(tel, buf_loc, "/sdcard/negato/vecchio.bin", NULL, NULL, NULL, &e) == PH_FILE_NON_CONSENTITO);
    CHECK(esiste(buf_rem));

    /* Cartella remota inesistente: come adbd, viene creata. */
    CHECK(ph_file_invia(tel, buf_loc, "/sdcard/nuova/sotto/x.bin", NULL, NULL, NULL, &e) == PH_FILE_RIUSCITO);
    finto_percorso(tel, "/sdcard/nuova/sotto/x.bin", buf_rem, sizeof buf_rem);
    CHECK(uguali(buf_loc, buf_rem));
    unlink(buf_rem);
    CHECK(strlen(buf_loc) > 0);
    /* Richiesta "percorso,modo" oltre i 1024 byte: rifiutata prima di aprire canali. */
    {
        char lungo[1100];
        memset(lungo, 'a', sizeof lungo);
        for (size_t i = 0; i < sizeof lungo; i += 101)
            lungo[i] = '/'; /* segmenti corti: i nomi hanno un limite */
        lungo[1019] = '\0'; /* 1019 + ",33188" = 1025 */
        unsigned ap0 = tel->canali_aperti;
        CHECK(ph_file_invia(tel, buf_loc, lungo, NULL, NULL, NULL, &e) == PH_FILE_ERRORE);
        CHECK(tel->canali_aperti == ap0);
        lungo[1018] = '\0'; /* 1024 esatti: accettata */
        CHECK(ph_file_invia(tel, buf_loc, lungo, NULL, NULL, NULL, &e) == PH_FILE_RIUSCITO);
    }

    /* File locale inesistente: errore, senza aprire canali. */
    unsigned ap = tel->canali_aperti;
    CHECK(ph_file_invia(tel, "/non/esiste/qui", "/sdcard/Download/x", NULL, NULL, NULL, &e) == PH_FILE_ERRORE);
    CHECK(tel->canali_aperti == ap);

    /* Connessione che cade a meta' invio: errore, parziale tolto. */
    tel->o.scrivi_errore_dopo = 100000;
    CHECK(ph_file_invia(tel, buf_loc, "/sdcard/Download/caduto.bin", NULL, NULL, NULL, &e) == PH_FILE_ERRORE);
    finto_percorso(tel, "/sdcard/Download/caduto.bin", buf_rem, sizeof buf_rem);
    CHECK(!esiste(buf_rem));
    finto_opzioni_normali(tel);

    /* Servizio rifiutato. */
    tel->o.rifiuta_apertura = true;
    CHECK(ph_file_invia(tel, buf_loc, "/sdcard/Download/x", NULL, NULL, NULL, &e) == PH_FILE_ERRORE);
    PhFileElenco el;
    CHECK(ph_file_elenca(tel, "/sdcard", &el, &e) == PH_FILE_ERRORE);
    finto_opzioni_normali(tel);

    /* Nome con apice, annullato in partenza: il parziale lo cancelliamo noi via
     * shell, e il quoting deve reggere. */
    atomic_bool gia = true;
    CHECK(ph_file_invia(tel, buf_loc, "/sdcard/Download/l'apice $(x).bin", NULL, NULL, &gia, &e) == PH_FILE_ANNULLATO);
    finto_percorso(tel, "/sdcard/Download/l'apice $(x).bin", buf_rem, sizeof buf_rem);
    CHECK(!esiste(buf_rem));
    finto_opzioni_normali(tel);

    /* FAIL del telefono: il parziale l'ha gia' tolto lui, noi non lanciamo rm. */
    tel->o.fail_invio = true;
    unsigned rm1 = tel->rm_eseguiti;
    CHECK(ph_file_invia(tel, buf_loc, "/sdcard/Download/pieno2.bin", NULL, NULL, NULL, &e) == PH_FILE_ERRORE);
    CHECK(tel->rm_eseguiti == rm1);
    finto_opzioni_normali(tel);
}

typedef struct {
    const char *da_creare; /* creato al primo avanzamento con dati */
    bool fatto;
} Intruso;

static void progresso_intruso(uint64_t fatti, uint64_t totale, void *u)
{
    Intruso *i = u;
    (void)totale;
    if (fatti > 0 && !i->fatto) {
        i->fatto = true;
        FILE *f = fopen(i->da_creare, "wb");
        CHECK(f);
        fputs("dell'utente", f);
        fclose(f);
    }
}

static bool contenuto_e(const char *p, const char *atteso)
{
    char b[64] = {0};
    FILE *f = fopen(p, "rb");
    if (!f)
        return false;
    size_t n = fread(b, 1, sizeof b - 1, f);
    fclose(f);
    return n == strlen(atteso) && memcmp(b, atteso, n) == 0;
}

static void scrivi_testo(const char *p, const char *t)
{
    FILE *f = fopen(p, "wb");
    CHECK(f);
    fputs(t, f);
    fclose(f);
}

extern int (*ph_file_prova_scambio)(int numero, const char *locale);

static int gancio_fallisci_ritorno;
static int gancio_scambio(int numero, const char *locale)
{
    if (numero == 1) {
        scrivi_testo(locale, "dell'utente"); /* la corsa: al posto della prenotazione compare un file */
        return 0;
    }
    return gancio_fallisci_ritorno;
}

/* Mai toccare file dell'utente in ricezione. */
static void prova_protezione_destinazione(void)
{
    PhErrore e = {{0}};
    char dest[600], parz[700], altro[600];
    percorso_loc(dest, sizeof dest, "dest.bin");
    snprintf(parz, sizeof parz, "%s.parziale", dest);
    percorso_loc(altro, sizeof altro, "bersaglio.txt");
    const char *rem = "/sdcard/Download/p.bin"; /* esiste: creato da prova_errori_ricezione */
    unsigned ap = tel->canali_aperti;

    /* Parziale gia' presente (file): ESISTE, intatto, telefono non contattato. */
    scrivi_testo(parz, "mio parziale");
    CHECK(ph_file_ricevi(tel, rem, dest, NULL, NULL, NULL, &e) == PH_FILE_ESISTE);
    CHECK(contenuto_e(parz, "mio parziale") && !esiste(dest) && tel->canali_aperti == ap);
    unlink(parz);

    /* Parziale gia' presente come collegamento simbolico: il bersaglio non si tocca. */
    scrivi_testo(altro, "bersaglio intatto");
    CHECK(symlink(altro, parz) == 0);
    CHECK(ph_file_ricevi(tel, rem, dest, NULL, NULL, NULL, &e) == PH_FILE_ESISTE);
    CHECK(contenuto_e(altro, "bersaglio intatto"));
    struct stat sb;
    CHECK(lstat(parz, &sb) == 0 && S_ISLNK(sb.st_mode) && !esiste(dest) && tel->canali_aperti == ap);
    unlink(parz);

    /* Collegamento simbolico spezzato come parziale: idem. */
    CHECK(symlink("/non/esiste/proprio", parz) == 0);
    CHECK(ph_file_ricevi(tel, rem, dest, NULL, NULL, NULL, &e) == PH_FILE_ESISTE);
    CHECK(lstat(parz, &sb) == 0 && S_ISLNK(sb.st_mode));
    unlink(parz);

    /* Destinazione non vuota gia' presente (o collegamento): ESISTE, intatta, nessun canale. */
    scrivi_testo(dest, "file dell'utente");
    CHECK(ph_file_ricevi(tel, rem, dest, NULL, NULL, NULL, &e) == PH_FILE_ESISTE);
    CHECK(contenuto_e(dest, "file dell'utente") && !esiste(parz) && tel->canali_aperti == ap);
    unlink(dest);
    CHECK(symlink(altro, dest) == 0);
    CHECK(ph_file_ricevi(tel, rem, dest, NULL, NULL, NULL, &e) == PH_FILE_ESISTE);
    CHECK(contenuto_e(altro, "bersaglio intatto") && !esiste(parz));
    unlink(dest);
    unlink(altro);

    /* Destinazione comparsa durante la copia: ESISTE, il file nuovo resta, parziale tolto. */
    Intruso in = {dest, false};
    CHECK(ph_file_ricevi(tel, rem, dest, progresso_intruso, &in, NULL, &e) == PH_FILE_ESISTE);
    CHECK(in.fatto && contenuto_e(dest, "dell'utente") && !esiste(parz));
    unlink(dest);

    /* Corsa vera sulla prenotazione: il file compare tra controllo e scambio. Lo
     * scambio di ritorno riesce: ESISTE, file dell'utente al suo posto, parziale tolto. */
    ph_file_prova_scambio = gancio_scambio;
    gancio_fallisci_ritorno = 0;
    scrivi_testo(dest, "");
    CHECK(ph_file_ricevi(tel, rem, dest, NULL, NULL, NULL, &e) == PH_FILE_ESISTE);
    CHECK(contenuto_e(dest, "dell'utente") && !esiste(parz));
    unlink(dest);
    /* Lo scambio di ritorno fallisce: il file dell'utente resta in .parziale (NON
     * cancellato), il file ricevuto e' a <locale>, e il messaggio lo dice. */
    gancio_fallisci_ritorno = 1;
    scrivi_testo(dest, "");
    CHECK(ph_file_ricevi(tel, rem, dest, NULL, NULL, NULL, &e) == PH_FILE_ERRORE);
    CHECK(contenuto_e(parz, "dell'utente") && esiste(dest));
    percorso_loc(altro, sizeof altro, "p.bin");
    CHECK(uguali(altro, dest));
    CHECK(strstr(e.testo, ".parziale") != NULL);
    unlink(dest);
    unlink(parz);
    ph_file_prova_scambio = NULL;

    /* Prenotazione da 0 byte: sostituita normalmente. */
    mode_t um_prima = umask(077); /* l'umask dell'utente deve valere */
    scrivi_testo(dest, "");
    CHECK(ph_file_ricevi(tel, rem, dest, NULL, NULL, NULL, &e) == PH_FILE_RIUSCITO);
    umask(um_prima);
    percorso_loc(altro, sizeof altro, "p.bin");
    CHECK(uguali(altro, dest) && !esiste(parz));
    CHECK(lstat(dest, &sb) == 0 && (sb.st_mode & 0777) == (0644 & ~(mode_t)077));
    CHECK(mtime_di(dest) == 1700000000);
    unlink(dest);

    /* Al posto della prenotazione compare un file non vuoto durante la copia:
     * ESISTE, il file dell'utente resta, parziale tolto. */
    scrivi_testo(dest, "");
    Intruso in2 = {dest, false};
    CHECK(ph_file_ricevi(tel, rem, dest, progresso_intruso, &in2, NULL, &e) == PH_FILE_ESISTE);
    CHECK(in2.fatto && contenuto_e(dest, "dell'utente") && !esiste(parz));
    unlink(dest);
}

static void prova_errori_ricezione(void)
{
    PhErrore e = {{0}};
    percorso_loc(buf_loc2, sizeof buf_loc2, "r.bin");
    char parz[700];
    snprintf(parz, sizeof parz, "%s.parziale", buf_loc2);

    CHECK(ph_file_ricevi(tel, "/sdcard/Download/nonesiste.bin", buf_loc2, NULL, NULL, NULL, &e) == PH_FILE_NON_TROVATO);
    CHECK(!esiste(parz) && !esiste(buf_loc2));
    finto_percorso(tel, "/sdcard/negato/vecchio.bin", buf_rem, sizeof buf_rem);
    CHECK(ph_file_ricevi(tel, "/sdcard/negato/vecchio.bin", buf_loc2, NULL, NULL, NULL, &e) == PH_FILE_NON_CONSENTITO);
    CHECK(!esiste(parz) && !esiste(buf_loc2));
    CHECK(ph_file_ricevi(tel, "/sdcard/Download", buf_loc2, NULL, NULL, NULL, &e) == PH_FILE_ERRORE); /* e' una cartella */
    CHECK(!esiste(parz));

    /* Cartella locale inesistente. */
    snprintf(buf_loc, sizeof buf_loc, "%s/non/esiste/r.bin", loc);
    percorso_loc(buf_rem, sizeof buf_rem, "p.bin");
    riempi(buf_rem, 10, 1, 1700000000);
    CHECK(ph_file_invia(tel, buf_rem, "/sdcard/Download/p.bin", NULL, NULL, NULL, &e) == PH_FILE_RIUSCITO);
    CHECK(ph_file_ricevi(tel, "/sdcard/Download/p.bin", buf_loc, NULL, NULL, NULL, &e) == PH_FILE_ERRORE);

    /* FAIL a meta' (file di 3 blocchi). */
    percorso_loc(buf_rem, sizeof buf_rem, "tre.bin");
    riempi(buf_rem, 3 * 65536, 2, 1700000000);
    CHECK(ph_file_invia(tel, buf_rem, "/sdcard/Download/tre.bin", NULL, NULL, NULL, &e) == PH_FILE_RIUSCITO);
    tel->o.fail_ricevi_a_meta = true;
    CHECK(ph_file_ricevi(tel, "/sdcard/Download/tre.bin", buf_loc2, NULL, NULL, NULL, &e) == PH_FILE_ERRORE);
    CHECK(strstr(e.testo, "I/O error") != NULL);
    CHECK(!esiste(parz) && !esiste(buf_loc2));
    finto_opzioni_normali(tel);

    /* Blocchi piccoli (1 byte per DATA): funziona lo stesso. */
    percorso_loc(buf_rem, sizeof buf_rem, "piccoli.bin");
    riempi(buf_rem, 3000, 4, 1700000000);
    CHECK(ph_file_invia(tel, buf_rem, "/sdcard/Download/piccoli.bin", NULL, NULL, NULL, &e) == PH_FILE_RIUSCITO);
    tel->o.blocco_ricevi = 1;
    CHECK(ph_file_ricevi(tel, "/sdcard/Download/piccoli.bin", buf_loc2, NULL, NULL, NULL, &e) == PH_FILE_RIUSCITO);
    CHECK(uguali(buf_rem, buf_loc2));
    unlink(buf_loc2);
    finto_opzioni_normali(tel);

    /* Blocco dichiarato di 2 GiB: rifiutato subito, senza allocare ne' aspettare. */
    tel->o.lunghezza_assurda = 0x7fffffffu;
    CHECK(ph_file_ricevi(tel, "/sdcard/Download/piccoli.bin", buf_loc2, NULL, NULL, NULL, &e) == PH_FILE_ERRORE);
    CHECK(!esiste(parz) && !esiste(buf_loc2));
    finto_opzioni_normali(tel);
}

static const PhFileVoce *cerca(const PhFileElenco *el, const char *nome)
{
    for (size_t i = 0; i < el->numero; i++)
        if (strcmp(el->voci[i].nome, nome) == 0)
            return &el->voci[i];
    return NULL;
}

static void prova_elenco(void)
{
    PhErrore e = {{0}};
    crea_dir_tel("/sdcard/Elenco/Zeta");
    crea_dir_tel("/sdcard/Elenco/alfa");
    char r[1200];
    static const char *file[] = {"b.txt", "A.txt", ".nascosto", "\xc3\xa8 accentata, con virgola.txt"};
    for (size_t i = 0; i < 4; i++) {
        char t[300];
        snprintf(t, sizeof t, "/sdcard/Elenco/%s", file[i]);
        finto_percorso(tel, t, r, sizeof r);
        riempi(r, 10 * (i + 1), i, 1700000000 + (time_t)i);
    }
    finto_percorso(tel, "/sdcard/Elenco/collegamento", r, sizeof r);
    CHECK(symlink("alfa", r) == 0);
    finto_percorso(tel, "/sdcard/Elenco/rotto", r, sizeof r);
    CHECK(symlink("non_esiste", r) == 0);

    PhFileElenco el;
    CHECK(ph_file_elenca(tel, "/sdcard/Elenco/", &el, &e) == PH_FILE_RIUSCITO);
    /* Cartelle prima (senza badare alle maiuscole), poi i file. Niente "." e "..". */
    static const char *atteso[] = {"alfa", "collegamento", "Zeta", ".nascosto", "A.txt", "b.txt", "rotto",
                                   "\xc3\xa8 accentata, con virgola.txt"};
    CHECK(el.numero == 8);
    for (size_t i = 0; i < 8; i++)
        CHECK(strcmp(el.voci[i].nome, atteso[i]) == 0);
    CHECK(el.voci[0].cartella && !el.voci[0].collegamento);
    CHECK(el.voci[1].cartella && el.voci[1].collegamento); /* link a cartella */
    CHECK(!el.voci[6].cartella && el.voci[6].collegamento); /* link rotto */
    const PhFileVoce *v = cerca(&el, "b.txt");
    CHECK(v && !v->cartella && !v->collegamento && v->dimensione == 10 && v->modificato == 1700000000);
    v = cerca(&el, "A.txt");
    CHECK(v && v->dimensione == 20);
    ph_file_elenco_libera(&el);
    CHECK(el.numero == 0 && el.voci == NULL);

    /* Cartella vuota; radice "/". */
    CHECK(ph_file_elenca(tel, "/sdcard/Elenco/alfa", &el, &e) == PH_FILE_RIUSCITO && el.numero == 0);
    ph_file_elenco_libera(&el);
    CHECK(ph_file_elenca(tel, "/", &el, &e) == PH_FILE_RIUSCITO && el.numero >= 1);
    ph_file_elenco_libera(&el);

    CHECK(ph_file_elenca(tel, "/sdcard/nonesiste", &el, &e) == PH_FILE_NON_TROVATO && el.numero == 0);
    CHECK(ph_file_elenca(tel, "/sdcard/negato", &el, &e) == PH_FILE_NON_CONSENTITO);
    CHECK(ph_file_elenca(tel, "/sdcard/Elenco/b.txt", &el, &e) == PH_FILE_ERRORE); /* non e' una cartella */

    /* Dimensioni oltre 4 GiB: elenco, info e progresso restano giusti. */
    uint64_t grande = 5ull * 1024 * 1024 * 1024 + 7;
    tel->o.dimensione_finta = grande;
    CHECK(ph_file_elenca(tel, "/sdcard/Elenco", &el, &e) == PH_FILE_RIUSCITO);
    v = cerca(&el, "b.txt");
    CHECK(v && v->dimensione == grande);
    ph_file_elenco_libera(&el);
    PhFileVoce info;
    CHECK(ph_file_info(tel, "/sdcard/Elenco/b.txt", &info, &e) == PH_FILE_RIUSCITO);
    CHECK(info.dimensione == grande && info.nome == NULL);
    Prog p;
    prog_init(&p);
    percorso_loc(buf_loc2, sizeof buf_loc2, "grande.out");
    /* Il file vero e' piccolo: la ricezione deve accorgersene (ERRORE) ma il
     * totale annunciato al progresso e' a 64 bit. */
    CHECK(ph_file_ricevi(tel, "/sdcard/Elenco/b.txt", buf_loc2, progresso, &p, NULL, &e) == PH_FILE_ERRORE);
    CHECK(p.totale == grande);
    snprintf(r, sizeof r, "%s.parziale", buf_loc2);
    CHECK(!esiste(r) && !esiste(buf_loc2));
    finto_opzioni_normali(tel);

    /* Informazioni. */
    CHECK(ph_file_info(tel, "/sdcard/Elenco/alfa", &info, &e) == PH_FILE_RIUSCITO && info.cartella && !info.collegamento);
    CHECK(ph_file_info(tel, "/sdcard/Elenco/collegamento", &info, &e) == PH_FILE_RIUSCITO && info.cartella &&
          info.collegamento);
    CHECK(ph_file_info(tel, "/sdcard/Elenco/rotto", &info, &e) == PH_FILE_RIUSCITO && !info.cartella &&
          info.collegamento);
    CHECK(ph_file_info(tel, "/sdcard/Elenco/A.txt", &info, &e) == PH_FILE_RIUSCITO && !info.cartella &&
          info.dimensione == 20 && info.modificato == 1700000001);
    CHECK(ph_file_info(tel, "/sdcard/nonesiste", &info, &e) == PH_FILE_NON_TROVATO);
    CHECK(ph_file_info(tel, "/sdcard/negato/x", &info, &e) == PH_FILE_NON_CONSENTITO);
    CHECK(ph_file_info(tel, "", &info, &e) == PH_FILE_ERRORE);
}

/* Risposte troncate e corrotte: nessun esito riuscito dove mancano byte
 * indispensabili, nessun blocco, e (con AddressSanitizer) nessuna lettura
 * fuori dai buffer. */
static void prova_malformate(void)
{
    PhErrore e = {{0}};
    PhFileElenco el;
    PhFileVoce info;
    crea_dir_tel("/sdcard/T");
    char r[1200];
    finto_percorso(tel, "/sdcard/T/uno", r, sizeof r);
    riempi(r, 10, 1, 1700000000);
    finto_percorso(tel, "/sdcard/T/due con spazio", r, sizeof r);
    riempi(r, 100, 2, 1700000000);
    percorso_loc(buf_loc2, sizeof buf_loc2, "m.bin");
    char parz[700];
    snprintf(parz, sizeof parz, "%s.parziale", buf_loc2);

    /* Lunghezze della risposta intera di ogni operazione. */
    CHECK(ph_file_info(tel, "/sdcard/T/uno", &info, &e) == PH_FILE_RIUSCITO);
    size_t t_info = tel->ultimo_prodotti;
    CHECK(ph_file_elenca(tel, "/sdcard/T", &el, &e) == PH_FILE_RIUSCITO && el.numero == 2);
    ph_file_elenco_libera(&el);
    size_t t_elenco = tel->ultimo_prodotti;
    CHECK(ph_file_ricevi(tel, "/sdcard/T/due con spazio", buf_loc2, NULL, NULL, NULL, &e) == PH_FILE_RIUSCITO);
    unlink(buf_loc2);
    size_t t_ricevi = tel->ultimo_prodotti;
    CHECK(t_info == 72 && t_elenco > 150 && t_ricevi == 72 + 8 + 100 + 8);

    /* Troncamento a ogni posizione, con il telefono che chiude. Del DONE finale
     * servono solo le prime 4 lettere: il resto non e' indispensabile. */
    for (size_t k = 0; k < t_info; k++) {
        tel->o.tronca_a = k;
        CHECK(ph_file_info(tel, "/sdcard/T/uno", &info, &e) != PH_FILE_RIUSCITO);
    }
    for (size_t k = 0; k < t_elenco - 72; k++) {
        tel->o.tronca_a = k;
        CHECK(ph_file_elenca(tel, "/sdcard/T", &el, &e) != PH_FILE_RIUSCITO);
        CHECK(el.numero == 0 && el.voci == NULL);
    }
    for (size_t k = 0; k < t_ricevi - 4; k++) {
        tel->o.tronca_a = k;
        CHECK(ph_file_ricevi(tel, "/sdcard/T/due con spazio", buf_loc2, NULL, NULL, NULL, &e) != PH_FILE_RIUSCITO);
        CHECK(!esiste(parz) && !esiste(buf_loc2));
    }
    /* Resta anche il caso "scritta prima della fine ma oltre il necessario". */
    tel->o.tronca_a = t_elenco - 72;
    CHECK(ph_file_elenca(tel, "/sdcard/T", &el, &e) == PH_FILE_RIUSCITO);
    ph_file_elenco_libera(&el);
    tel->o.tronca_a = t_ricevi - 4;
    CHECK(ph_file_ricevi(tel, "/sdcard/T/due con spazio", buf_loc2, NULL, NULL, NULL, &e) == PH_FILE_RIUSCITO);
    unlink(buf_loc2);

    /* Telefono che tace: errore dopo l'attesa breve, non un blocco. */
    tel->o.stallo = true;
    size_t campioni[] = {0, 5, 71, 80};
    for (size_t i = 0; i < sizeof campioni / sizeof *campioni; i++) {
        tel->o.tronca_a = campioni[i];
        struct timespec a, b;
        clock_gettime(CLOCK_MONOTONIC, &a);
        CHECK(ph_file_elenca(tel, "/sdcard/T", &el, &e) == PH_FILE_ERRORE);
        CHECK(ph_file_info(tel, "/sdcard/T/uno", &info, &e) == PH_FILE_ERRORE || campioni[i] >= 72);
        CHECK(ph_file_ricevi(tel, "/sdcard/T/due con spazio", buf_loc2, NULL, NULL, NULL, &e) == PH_FILE_ERRORE);
        clock_gettime(CLOCK_MONOTONIC, &b);
        CHECK(b.tv_sec - a.tv_sec < 10);
        CHECK(!esiste(parz) && !esiste(buf_loc2));
    }
    finto_opzioni_normali(tel);

    /* Un telefono che tace si puo' comunque annullare. */
    tel->o.stallo = true;
    tel->o.tronca_a = 80;
    atomic_bool annulla = true;
    CHECK(ph_file_ricevi(tel, "/sdcard/T/due con spazio", buf_loc2, NULL, NULL, &annulla, &e) == PH_FILE_ANNULLATO);
    CHECK(!esiste(parz) && !esiste(buf_loc2));
    finto_opzioni_normali(tel);

    /* Un byte invertito, a ogni posizione: puo' riuscire o fallire, mai crollare. */
    for (size_t k = 0; k < t_info; k++) {
        tel->o.corrompi = (long)k;
        (void)ph_file_info(tel, "/sdcard/T/uno", &info, &e);
    }
    for (size_t k = 0; k < t_elenco; k++) {
        tel->o.corrompi = (long)k;
        PhFileEsito es = ph_file_elenca(tel, "/sdcard/T", &el, &e);
        if (es != PH_FILE_RIUSCITO)
            CHECK(el.numero == 0 && el.voci == NULL);
        for (size_t i = 0; es == PH_FILE_RIUSCITO && i < el.numero; i++)
            CHECK(el.voci[i].nome && strchr(el.voci[i].nome, '/') == NULL);
        ph_file_elenco_libera(&el);
    }
    for (size_t k = 0; k < t_ricevi; k++) {
        tel->o.corrompi = (long)k;
        PhFileEsito es = ph_file_ricevi(tel, "/sdcard/T/due con spazio", buf_loc2, NULL, NULL, NULL, &e);
        CHECK(!esiste(parz));
        CHECK((es == PH_FILE_RIUSCITO) == esiste(buf_loc2));
        unlink(buf_loc2);
    }
    finto_opzioni_normali(tel);
}

static int cancella(const char *p, const struct stat *sb, int tipo, struct FTW *f)
{
    (void)sb;
    (void)tipo;
    (void)f;
    return remove(p);
}

int main(int argc, char **argv)
{
    tel = finto_nuovo();
    CHECK(tel != NULL);
    const char *base = getenv("TMPDIR");
    snprintf(loc, sizeof loc, "%s/phfile_locale_XXXXXX", base ? base : ".");
    CHECK(mkdtemp(loc) != NULL);

    prova_andata_e_ritorno();
    if (argc > 1 && strcmp(argv[1], "corte") == 0) {
        /* Libreria con letture corte: bastano andata e ritorno e l'annullamento. */
        prova_annullamento();
        CHECK(tel->canali_aperti == tel->canali_chiusi);
        nftw(loc, cancella, 16, FTW_DEPTH | FTW_PHYS);
        finto_libera(tel);
        puts("prova_file (letture corte): tutto bene");
        return 0;
    }
    prova_annullamento();
    prova_errori_invio();
    prova_errori_ricezione();
    prova_protezione_destinazione();
    prova_elenco();
    prova_malformate();

    /* Nessun canale lasciato aperto. */
    CHECK(tel->canali_aperti == tel->canali_chiusi);
    nftw(loc, cancella, 16, FTW_DEPTH | FTW_PHYS);
    finto_libera(tel);
    puts("prova_file: tutto bene");
    return 0;
}
