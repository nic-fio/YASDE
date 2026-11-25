/* Vedi finto.h. */
#include "finto.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "file/file.h"

struct PhAdb {
    atomic_bool viva;
};

static pthread_mutex_t m = PTHREAD_MUTEX_INITIALIZER;
static char radice[PATH_MAX];
static int ritardo;
static int shell_ko, shell_ritardo, corsa, sostituisci;
static char **comandi;
static size_t n_comandi;
static atomic_int in_corso, max_corso;

void finto_radice(const char *dir)
{
    pthread_mutex_lock(&m);
    snprintf(radice, sizeof radice, "%s", dir);
    pthread_mutex_unlock(&m);
}

void finto_ritardo_us(int us)
{
    pthread_mutex_lock(&m);
    ritardo = us;
    pthread_mutex_unlock(&m);
}

void finto_shell_fallisce(int si)
{
    pthread_mutex_lock(&m);
    shell_ko = si;
    pthread_mutex_unlock(&m);
}

void finto_shell_ritardo_ms(int ms)
{
    pthread_mutex_lock(&m);
    shell_ritardo = ms;
    pthread_mutex_unlock(&m);
}

void finto_corsa_parziale(int si)
{
    pthread_mutex_lock(&m);
    corsa = si;
    pthread_mutex_unlock(&m);
}

void finto_sostituisci_prenotazione(int si)
{
    pthread_mutex_lock(&m);
    sostituisci = si;
    pthread_mutex_unlock(&m);
}

void finto_reset(void)
{
    pthread_mutex_lock(&m);
    for (size_t i = 0; i < n_comandi; i++)
        free(comandi[i]);
    free(comandi);
    comandi = NULL;
    n_comandi = 0;
    ritardo = 0;
    shell_ko = 0;
    shell_ritardo = 0;
    corsa = 0;
    sostituisci = 0;
    pthread_mutex_unlock(&m);
    atomic_store(&in_corso, 0);
    atomic_store(&max_corso, 0);
}

PhAdb *finto_adb_nuovo(void)
{
    PhAdb *a = calloc(1, sizeof *a);
    if (a)
        atomic_init(&a->viva, true);
    return a;
}

void finto_adb_libera(PhAdb *a) { free(a); }
void finto_adb_uccidi(PhAdb *a) { atomic_store(&a->viva, false); }
bool ph_adb_viva(PhAdb *a) { return atomic_load(&a->viva); }

int finto_copie_contemporanee_max(void) { return atomic_load(&max_corso); }

size_t finto_comandi_numero(void)
{
    pthread_mutex_lock(&m);
    size_t n = n_comandi;
    pthread_mutex_unlock(&m);
    return n;
}

char *finto_comando(size_t i)
{
    char *r = NULL;
    pthread_mutex_lock(&m);
    if (i < n_comandi)
        r = strdup(comandi[i]);
    pthread_mutex_unlock(&m);
    return r;
}

void finto_percorso_reale(const char *tel, char *out, size_t cap)
{
    pthread_mutex_lock(&m);
    snprintf(out, cap, "%s%s", radice, tel);
    pthread_mutex_unlock(&m);
}

int ph_shell_esegui(PhAdb *a, const char *comando, PhBuffer *uscita, PhBuffer *errori, int *codice,
                    int timeout_ms, PhErrore *e)
{
    (void)uscita;
    (void)errori;
    (void)timeout_ms;
    if (codice)
        *codice = 0;
    pthread_mutex_lock(&m);
    int pausa_ms = shell_ritardo;
    pthread_mutex_unlock(&m);
    if (pausa_ms > 0)
        usleep((useconds_t)pausa_ms * 1000);
    /* dopo la pausa: se la PhAdb fosse stata liberata, ASan lo vedrebbe qui */
    if (!atomic_load(&a->viva))
        return ph_errore(e, "finto: connessione chiusa");
    pthread_mutex_lock(&m);
    bool ko = shell_ko;
    if (!ko) {
        char **nuovo = realloc(comandi, (n_comandi + 1) * sizeof *nuovo);
        char *c = strdup(comando);
        if (nuovo && c) {
            comandi = nuovo;
            comandi[n_comandi++] = c;
        } else {
            free(c);
        }
    }
    pthread_mutex_unlock(&m);
    if (ko)
        return ph_errore(e, "finto: shell non disponibile");
    return 0;
}

static void crea_cartelle(const char *percorso)
{
    char buf[PATH_MAX];
    snprintf(buf, sizeof buf, "%s", percorso);
    char *ult = strrchr(buf, '/');
    if (!ult)
        return;
    *ult = '\0';
    for (char *p = buf + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(buf, 0755);
            *p = '/';
        }
    }
    mkdir(buf, 0755);
}

static void entra(void)
{
    int n = atomic_fetch_add(&in_corso, 1) + 1;
    int mx = atomic_load(&max_corso);
    while (n > mx && !atomic_compare_exchange_weak(&max_corso, &mx, n))
        ;
}

static void esci(void) { atomic_fetch_sub(&in_corso, 1); }

/* Copia comune: da in a out_percorso (file da creare), con le regole di
 * fallimento e di lentezza. Se riesce ritorna RIUSCITO e lascia il file;
 * altrimenti lo toglie. */
static PhFileEsito copia(PhAdb *a, const char *nome, const char *da, const char *verso, uint64_t *mod,
                         bool esclusivo, PhFileProgresso prog, void *utente, const atomic_bool *annulla, PhErrore *e)
{
    int in = open(da, O_RDONLY | O_CLOEXEC);
    if (in < 0)
        return ph_errore(e, "finto: apertura di %s: %s", da, strerror(errno)), PH_FILE_NON_TROVATO;
    struct stat st;
    fstat(in, &st);
    *mod = (uint64_t)st.st_mtime;
    crea_cartelle(verso);
    int out = open(verso, O_WRONLY | O_CREAT | O_CLOEXEC | (esclusivo ? O_EXCL | O_NOFOLLOW : O_TRUNC), 0644);
    if (out < 0) {
        int er = errno;
        close(in);
        if (esclusivo && (er == EEXIST || er == ELOOP))
            return ph_errore(e, "%s esiste gia' sul PC: non lo sovrascrivo", verso), PH_FILE_ESISTE;
        return ph_errore(e, "finto: creazione di %s: %s", verso, strerror(er)), PH_FILE_ERRORE;
    }
    PhFileEsito es = PH_FILE_RIUSCITO;
    char buf[FINTO_BLOCCO];
    uint64_t fatti = 0;
    pthread_mutex_lock(&m);
    int pausa = ritardo;
    pthread_mutex_unlock(&m);
    for (;;) {
        if (annulla && atomic_load(annulla)) {
            es = PH_FILE_ANNULLATO;
            break;
        }
        if (!atomic_load(&a->viva)) {
            ph_errore(e, "finto: connessione persa");
            es = PH_FILE_ERRORE;
            break;
        }
        ssize_t r = read(in, buf, sizeof buf);
        if (r < 0) {
            ph_errore(e, "finto: lettura");
            es = PH_FILE_ERRORE;
            break;
        }
        if (r == 0)
            break;
        if (write(out, buf, (size_t)r) != r) {
            ph_errore(e, "finto: scrittura");
            es = PH_FILE_ERRORE;
            break;
        }
        fatti += (uint64_t)r;
        if (prog)
            prog(fatti, (uint64_t)st.st_size, utente);
        if (fatti >= 8192 && strstr(nome, "muori")) {
            atomic_store(&a->viva, false);
            ph_errore(e, "finto: il telefono non risponde piu'");
            es = PH_FILE_ERRORE;
            break;
        }
        if (fatti >= 8192 && strstr(nome, "rotto")) {
            ph_errore(e, "Il telefono ha rifiutato la scrittura");
            es = PH_FILE_ERRORE;
            break;
        }
        if (pausa > 0)
            usleep((useconds_t)pausa);
    }
    close(in);
    if (close(out) != 0 && es == PH_FILE_RIUSCITO)
        es = PH_FILE_ERRORE;
    if (es != PH_FILE_RIUSCITO)
        unlink(verso);
    return es;
}

PhFileEsito ph_file_invia(PhAdb *a, const char *locale, const char *remoto, PhFileProgresso prog,
                          void *utente, const atomic_bool *annulla, PhErrore *e)
{
    char reale[PATH_MAX];
    finto_percorso_reale(remoto, reale, sizeof reale);
    entra();
    uint64_t mod = 0;
    PhFileEsito es = copia(a, locale, locale, reale, &mod, false, prog, utente, annulla, e);
    if (es == PH_FILE_RIUSCITO) {
        struct timespec t[2] = {{(time_t)mod, 0}, {(time_t)mod, 0}};
        utimensat(AT_FDCWD, reale, t, 0);
    }
    esci();
    return es;
}

PhFileEsito ph_file_ricevi(PhAdb *a, const char *remoto, const char *locale, PhFileProgresso prog,
                           void *utente, const atomic_bool *annulla, PhErrore *e)
{
    /* Contratto di ph_file_ricevi vero: mai sovrascrivere un file dell'utente.
     * Un "locale" esistente e' accettato solo se e' un file normale vuoto
     * (la prenotazione della coda); il parziale si crea in modo esclusivo. */
    char reale[PATH_MAX], parz[PATH_MAX + 16];
    finto_percorso_reale(remoto, reale, sizeof reale);
    snprintf(parz, sizeof parz, "%s.parziale", locale);
    struct stat sb;
    if (lstat(locale, &sb) == 0 && !(S_ISREG(sb.st_mode) && sb.st_size == 0))
        return ph_errore(e, "%s esiste gia' sul PC: non lo sovrascrivo", locale), PH_FILE_ESISTE;
    entra();
    pthread_mutex_lock(&m);
    bool fai_corsa = corsa && strstr(remoto, "corsa");
    if (fai_corsa)
        corsa = 0;
    pthread_mutex_unlock(&m);
    if (fai_corsa) {
        FILE *f = fopen(parz, "wb");
        if (f) {
            fputs("ALTRUI", f);
            fclose(f);
        }
    }
    uint64_t mod = 0;
    PhFileEsito es = copia(a, remoto, reale, parz, &mod, true, prog, utente, annulla, e);
    if (es == PH_FILE_RIUSCITO) {
        pthread_mutex_lock(&m);
        bool sost = sostituisci && strstr(remoto, "sosti");
        if (sost)
            sostituisci = 0;
        pthread_mutex_unlock(&m);
        if (sost) { /* l'utente mette un suo file al posto della prenotazione */
            unlink(locale);
            FILE *f = fopen(locale, "wb");
            if (f) {
                fputs("UTENTE", f);
                fclose(f);
            }
        }
        struct timespec t[2] = {{(time_t)mod, 0}, {(time_t)mod, 0}};
        utimensat(AT_FDCWD, parz, t, 0);
        if (lstat(locale, &sb) == 0 && !(S_ISREG(sb.st_mode) && sb.st_size == 0)) {
            unlink(parz);
            ph_errore(e, "%s esiste gia' sul PC: non lo sovrascrivo", locale);
            es = PH_FILE_ESISTE;
        } else if (rename(parz, locale) != 0) {
            unlink(parz);
            ph_errore(e, "finto: rinomina");
            es = PH_FILE_ERRORE;
        }
    }
    esci();
    return es;
}

PhFileEsito ph_file_info(PhAdb *a, const char *percorso, PhFileVoce *voce, PhErrore *e)
{
    if (!atomic_load(&a->viva))
        return ph_errore(e, "finto: connessione persa"), PH_FILE_ERRORE;
    char reale[PATH_MAX];
    finto_percorso_reale(percorso, reale, sizeof reale);
    struct stat st;
    if (stat(reale, &st) != 0)
        return ph_errore(e, "%s: non trovato", percorso), PH_FILE_NON_TROVATO;
    voce->nome = NULL;
    voce->cartella = S_ISDIR(st.st_mode);
    voce->collegamento = false;
    voce->dimensione = (uint64_t)st.st_size;
    voce->modificato = st.st_mtime;
    return PH_FILE_RIUSCITO;
}
