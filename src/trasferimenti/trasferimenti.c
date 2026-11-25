/* Coda dei trasferimenti: un thread di lavoro, un mutex, una condizione.
 * Regole di blocco: il mutex si tiene solo per leggere/scrivere lo stato; mai
 * durante una copia, una chiamata al telefono o un avviso. Le voci in corso
 * non vengono mai tolte dall'elenco, quindi il thread di lavoro puo' tenere il
 * puntatore alla sua voce anche a mutex rilasciato. */
#include "trasferimenti/trasferimenti.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "file/file.h"

#define NOME_MAX_BYTE 255          /* limite dei nomi di file (Linux e Android) */
#define SUFFISSO_PARZIALE ".parziale" /* lo aggiunge ph_file_ricevi al file di destinazione */
#define AVVISO_INTERVALLO_MS 100   /* al massimo ~10 avvisi di avanzamento al secondo */
#define TENTATIVI_NOME 100000
#define SCANNER_TIMEOUT_MS 3000
#define PERCORSO_BUF 4096

typedef struct {
    uint64_t id;
    PhTrVerso verso;
    PhTrStato stato;
    char *base;          /* nome originale, da cui si ricavano i nomi con il numero */
    char *nome;          /* nome finale */
    char *origine;
    char *cartella;      /* cartella di destinazione (telefono: gia' pulita) */
    char *destinazione;
    char *errore;        /* mai NULL dopo la creazione */
    uint64_t dimensione;
    uint64_t fatti;
    uint64_t ultimo_ms;  /* ultimo avviso inviato */
    unsigned indice;     /* numero da cui riprovare i nomi liberi */
    dev_t pren_dev;      /* file vuoto di prenotazione creato da noi (solo ricezione) */
    ino_t pren_ino;
} V;

struct PhTrasferimenti {
    pthread_mutex_t m;
    pthread_cond_t c;
    pthread_t thread;
    bool thread_vivo;
    V **voci;
    size_t numero, capacita;
    uint64_t prossimo_id;
    bool chiudi;
    bool ferma;
    PhAdb *adb;
    V *corrente;
    bool worker_noto;
    pthread_t worker;      /* identita' del thread di lavoro (per riconoscere le chiamate dall'avviso) */
    bool uso;              /* il thread di lavoro sta usando t->adb (dall'inizio di una voce alla fine dello scanner) */
    atomic_bool annulla;   /* della voce in corso; azzerato a ogni partenza */
    PhTrAvviso avviso;
    void *utente;
};

/* ---- Utilita' -------------------------------------------------------------- */

static uint64_t ora_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000u + (uint64_t)t.tv_nsec / 1000000u;
}

static char *dup_s(const char *s)
{
    size_t n = strlen(s) + 1;
    char *p = malloc(n);
    if (p)
        memcpy(p, s, n);
    return p;
}

static char *unisci(const char *cartella, const char *nome)
{
    size_t lc = strlen(cartella), ln = strlen(nome);
    bool barra = lc > 0 && cartella[lc - 1] == '/';
    char *p = malloc(lc + ln + 2);
    if (!p)
        return NULL;
    memcpy(p, cartella, lc);
    if (!barra)
        p[lc++] = '/';
    memcpy(p + lc, nome, ln + 1);
    return p;
}

static const char *ultimo_pezzo(const char *percorso)
{
    const char *s = strrchr(percorso, '/');
    return s ? s + 1 : percorso;
}

static bool terminato(PhTrStato s)
{
    return s == PH_TR_COMPLETATO || s == PH_TR_FALLITO || s == PH_TR_ANNULLATO;
}

static void v_libera(V *v)
{
    if (!v)
        return;
    free(v->base);
    free(v->nome);
    free(v->origine);
    free(v->cartella);
    free(v->destinazione);
    free(v->errore);
    free(v);
}

/* ---- Funzioni pure ---------------------------------------------------------- */

int ph_tr_percorso_telefono(const char *in, char *out, size_t capacita, PhErrore *e)
{
    if (!in || !out || capacita == 0)
        return ph_errore(e, "percorso del telefono: argomenti mancanti");
    if (in[0] != '/')
        return ph_errore(e, "Il percorso sul telefono deve iniziare con \"/\": %s", in);
    static const char scorciatoia[] = "/sdcard";
    static const char vero[] = "/storage/emulated/0";
    const char *resto = in;
    size_t pos = 0;
    if (strncmp(in, scorciatoia, sizeof scorciatoia - 1) == 0 &&
        (in[sizeof scorciatoia - 1] == '\0' || in[sizeof scorciatoia - 1] == '/')) {
        resto = in + sizeof scorciatoia - 1;
        if (sizeof vero >= capacita)
            return ph_errore(e, "Percorso sul telefono troppo lungo");
        memcpy(out, vero, sizeof vero - 1);
        pos = sizeof vero - 1;
    }
    for (const char *p = resto; *p; p++) {
        if (*p == '/' && pos > 0 && out[pos - 1] == '/')
            continue;
        if (pos + 1 >= capacita)
            return ph_errore(e, "Percorso sul telefono troppo lungo");
        out[pos++] = *p;
    }
    while (pos > 1 && out[pos - 1] == '/')
        pos--;
    out[pos] = '\0';
    return 0;
}

static bool compressione(const char *s)
{
    static const char *const note[] = {"gz", "bz2", "xz", "zst", "lz", "lzma", "lz4", "br", "z"};
    for (size_t i = 0; i < sizeof note / sizeof *note; i++)
        if (strcasecmp(s, note[i]) == 0)
            return true;
    return false;
}

/* Posizione dell'estensione in nome (strlen(nome) se non ce n'e'). */
static size_t inizio_estensione(const char *nome)
{
    size_t len = strlen(nome);
    const char *ult = strrchr(nome, '.');
    if (!ult || ult == nome || ult[1] == '\0' || strlen(ult + 1) > 8)
        return len;
    for (const char *p = ult + 1; *p; p++)
        if ((unsigned char)*p <= 0x20 || *p == 0x7f)
            return len;
    size_t ext = (size_t)(ult - nome);
    if (compressione(ult + 1)) {
        size_t j = ext;
        while (j > 0 && nome[j - 1] != '.')
            j--;
        /* ".tar" subito prima, e quel punto non e' il primo carattere */
        if (j >= 2 && ext - j == 3 && strncasecmp(nome + j, "tar", 3) == 0)
            ext = j - 1;
    }
    return ext;
}

/* Come ph_tr_nome_con_indice, con il limite di lunghezza del nome scelto da chi
 * chiama (in ricezione il nome piu' ".parziale" deve stare nel limite). */
static int nome_indice(const char *nome, unsigned n, char *out, size_t capacita, size_t massimo)
{
    if (!nome || !out || nome[0] == '\0')
        return -1;
    size_t len = strlen(nome);
    if (n == 0) {
        if (len + 1 > capacita || len > massimo)
            return -1;
        memcpy(out, nome, len + 1);
        return 0;
    }
    char suff[32];
    int ls = snprintf(suff, sizeof suff, " (%u)", n);
    if (ls < 0 || (size_t)ls >= sizeof suff)
        return -1;
    size_t ext = inizio_estensione(nome);
    size_t coda = len - ext;
    size_t stem = ext;
    if (stem + (size_t)ls + coda > massimo) {
        if ((size_t)ls + coda >= massimo)
            return -1;
        stem = massimo - (size_t)ls - coda;
        while (stem > 0 && ((unsigned char)nome[stem] & 0xC0) == 0x80)
            stem--; /* non spezzare un carattere UTF-8 */
    }
    if (stem == 0 || stem + (size_t)ls + coda + 1 > capacita)
        return -1;
    memcpy(out, nome, stem);
    memcpy(out + stem, suff, (size_t)ls);
    memcpy(out + stem + (size_t)ls, nome + ext, coda + 1);
    return 0;
}

int ph_tr_nome_con_indice(const char *nome, unsigned n, char *out, size_t capacita)
{
    return nome_indice(nome, n, out, capacita, NOME_MAX_BYTE);
}

static size_t nome_massimo(PhTrVerso verso)
{
    return verso == PH_TR_RICEZIONE ? NOME_MAX_BYTE - (sizeof SUFFISSO_PARZIALE - 1) : NOME_MAX_BYTE;
}

/* Sul PC un nome e' libero solo se lo sono sia dest sia dest.parziale (lo
 * stesso del file in arrivo: ph_file_ricevi lo crea in modo esclusivo e non
 * deve mai calpestare un file dell'utente). */
static bool pc_occupato_parziale(const char *dest);

static bool pc_occupato(const char *dest)
{
    struct stat st;
    if (lstat(dest, &st) == 0)
        return true;
    return pc_occupato_parziale(dest);
}

int ph_tr_comando_scanner(const char *percorso, char *out, size_t capacita)
{
    if (!percorso || !out || percorso[0] != '/')
        return -1;
    static const char testa[] = "am broadcast -a android.intent.action.MEDIA_SCANNER_SCAN_FILE -d 'file://";
    size_t pos = sizeof testa - 1;
    if (pos >= capacita)
        return -1;
    memcpy(out, testa, pos);
    static const char esa[] = "0123456789ABCDEF";
    for (const unsigned char *p = (const unsigned char *)percorso; *p; p++) {
        bool semplice = (*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') ||
                        *p == '-' || *p == '.' || *p == '_' || *p == '~' || *p == '/';
        size_t serve = semplice ? 1 : 3;
        if (pos + serve + 2 > capacita)
            return -1;
        if (semplice) {
            out[pos++] = (char)*p;
        } else {
            out[pos++] = '%';
            out[pos++] = esa[*p >> 4];
            out[pos++] = esa[*p & 15];
        }
    }
    /* Dopo la codifica nell'indirizzo non resta alcun apice, ma se ne ci fosse
     * uno andrebbe scritto '\'' : qui non puo' succedere, quindi niente ramo. */
    out[pos++] = '\'';
    out[pos] = '\0';
    return 0;
}

/* ---- Copie di voci e avvisi ------------------------------------------------- */

static int voce_copia(const V *v, PhTrVoce *o)
{
    memset(o, 0, sizeof *o);
    o->id = v->id;
    o->verso = v->verso;
    o->stato = v->stato;
    o->dimensione = v->dimensione;
    o->fatti = v->fatti;
    o->nome = dup_s(v->nome);
    o->origine = dup_s(v->origine);
    o->destinazione = dup_s(v->destinazione);
    o->errore = dup_s(v->errore);
    if (!o->nome || !o->origine || !o->destinazione || !o->errore) {
        free(o->nome);
        free(o->origine);
        free(o->destinazione);
        free(o->errore);
        memset(o, 0, sizeof *o);
        return -1;
    }
    return 0;
}

static void voce_pub_libera(PhTrVoce *o)
{
    free(o->nome);
    free(o->origine);
    free(o->destinazione);
    free(o->errore);
    memset(o, 0, sizeof *o);
}

/* Chiamare con il mutex preso: copia la voce, rilascia il mutex, chiama
 * l'avviso, riprende il mutex. Dopo il ritorno v potrebbe non esistere piu'
 * se e' terminata: non toccarla. */
static void avvisa(PhTrasferimenti *t, V *v)
{
    if (!t->avviso)
        return;
    v->ultimo_ms = ora_ms();
    PhTrVoce copia;
    if (voce_copia(v, &copia) != 0)
        return; /* memoria finita: l'avviso si perde, lo stato resta leggibile */
    pthread_mutex_unlock(&t->m);
    t->avviso(&copia, t->utente);
    voce_pub_libera(&copia);
    pthread_mutex_lock(&t->m);
}

static bool pc_occupato_parziale(const char *dest)
{
    struct stat st;
    size_t n = strlen(dest) + sizeof SUFFISSO_PARZIALE;
    char *p = malloc(n);
    if (!p)
        return false;
    snprintf(p, n, "%s%s", dest, SUFFISSO_PARZIALE);
    bool r = lstat(p, &st) == 0;
    free(p);
    return r;
}

/* Mutex preso: avviso "la coda si e' fermata" (voce NULL). */
static void avvisa_coda(PhTrasferimenti *t)
{
    if (!t->avviso)
        return;
    pthread_mutex_unlock(&t->m);
    t->avviso(NULL, t->utente);
    pthread_mutex_lock(&t->m);
}

/* ---- Lavoro ---------------------------------------------------------------- */

/* Mutex preso. Un'altra voce viva ha gia' scelto questa destinazione? */
static bool riservato(const PhTrasferimenti *t, const V *self, const char *dest)
{
    for (size_t i = 0; i < t->numero; i++) {
        const V *o = t->voci[i];
        if (o == self || o->verso != self->verso)
            continue;
        if (o->stato != PH_TR_ATTESA && o->stato != PH_TR_CORSO)
            continue;
        /* il telefono (archivio di Android) non distingue maiuscole e minuscole */
        int d = self->verso == PH_TR_INVIO ? strcasecmp(o->destinazione, dest) : strcmp(o->destinazione, dest);
        if (d == 0)
            return true;
    }
    return false;
}

typedef struct {
    PhTrasferimenti *t;
    V *v;
} Ctx;

static void progresso(uint64_t fatti, uint64_t totale, void *utente)
{
    Ctx *c = utente;
    PhTrasferimenti *t = c->t;
    pthread_mutex_lock(&t->m);
    c->v->fatti = fatti;
    if (totale > 0)
        c->v->dimensione = totale;
    if (ora_ms() - c->v->ultimo_ms >= AVVISO_INTERVALLO_MS)
        avvisa(t, c->v);
    pthread_mutex_unlock(&t->m);
}

/* Sceglie il nome libero e aggiorna la voce. *creato = true se sul PC si e'
 * creato il file vuoto di prenotazione (da togliere se la copia non riesce).
 * Ogni candidato si scrive in v->destinazione sotto blocco PRIMA di
 * interrogare il telefono: cosi' un'altra voce non mostra mai lo stesso nome. */
static PhFileEsito scegli_destinazione(PhTrasferimenti *t, V *v, PhAdb *adb, bool *creato, PhErrore *e)
{
    for (unsigned n = v->indice; n < v->indice + TENTATIVI_NOME; n++) {
        if (atomic_load(&t->annulla))
            return PH_FILE_ANNULLATO;
        char nome[NOME_MAX_BYTE + 1];
        if (nome_indice(v->base, n, nome, sizeof nome, nome_massimo(v->verso)) != 0) {
            ph_errore(e, "Nome di file non utilizzabile: %s", v->base);
            return PH_FILE_ERRORE;
        }
        char *dest = unisci(v->cartella, nome);
        char *nn = dup_s(nome);
        if (!dest || !nn) {
            free(dest);
            free(nn);
            ph_errore(e, "Memoria esaurita");
            return PH_FILE_ERRORE;
        }
        pthread_mutex_lock(&t->m);
        bool preso = riservato(t, v, dest);
        if (!preso) {
            free(v->destinazione);
            free(v->nome);
            v->destinazione = dest;
            v->nome = nn;
            v->indice = n;
        }
        pthread_mutex_unlock(&t->m);
        if (preso) {
            free(dest);
            free(nn);
            continue;
        }
        /* da qui dest e nn appartengono alla voce */
        if (v->verso == PH_TR_INVIO) {
            PhFileVoce info;
            PhFileEsito es = ph_file_info(adb, v->destinazione, &info, e);
            if (es == PH_FILE_NON_TROVATO) {
                if (e)
                    e->testo[0] = '\0';
                return PH_FILE_RIUSCITO;
            }
            if (es != PH_FILE_RIUSCITO)
                return es == PH_FILE_ANNULLATO ? es : PH_FILE_ERRORE;
        } else if (!pc_occupato(v->destinazione)) {
            int fd = open(v->destinazione, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0644);
            if (fd >= 0) {
                struct stat sf;
                if (fstat(fd, &sf) == 0) {
                    v->pren_dev = sf.st_dev;
                    v->pren_ino = sf.st_ino;
                    *creato = true;
                } else {
                    unlink(v->destinazione); /* appena creato da noi */
                    close(fd);
                    ph_errore(e, "Impossibile creare %s: %s", v->destinazione, strerror(errno));
                    return PH_FILE_ERRORE;
                }
                close(fd);
                return PH_FILE_RIUSCITO;
            }
            if (errno == ENOENT || errno == ENOTDIR) {
                ph_errore(e, "La cartella di destinazione sul PC non esiste: %s", v->cartella);
                return PH_FILE_ERRORE;
            }
            if (errno != EEXIST) {
                ph_errore(e, "Impossibile creare %s: %s", v->destinazione, strerror(errno));
                return PH_FILE_ERRORE;
            }
        }
    }
    ph_errore(e, "Nessun nome libero trovato per %s", v->base);
    return PH_FILE_ERRORE;
}

/* Toglie la prenotazione solo se al nome c'e' ancora lo stesso file normale
 * di 0 byte che abbiamo creato: se nel frattempo c'e' qualcos'altro (un file
 * dell'utente) non si tocca nulla. */
static void togli_prenotazione(const V *v)
{
    struct stat st;
    if (lstat(v->destinazione, &st) == 0 && S_ISREG(st.st_mode) && st.st_size == 0 &&
        st.st_dev == v->pren_dev && st.st_ino == v->pren_ino)
        unlink(v->destinazione);
}

static void avvisa_scanner(PhAdb *adb, const char *dest)
{
    size_t cap = strlen(dest) * 3 + 128;
    char *cmd = malloc(cap);
    if (!cmd)
        return;
    if (ph_tr_comando_scanner(dest, cmd, cap) != 0) {
        free(cmd);
        return;
    }
    PhErrore ignora = {{0}};
    int codice = 0;
    /* Non fatale: il file e' gia' sul telefono, al peggio comparira' in
     * Galleria piu' tardi. */
    (void)ph_shell_esegui(adb, cmd, NULL, NULL, &codice, SCANNER_TIMEOUT_MS, &ignora);
    free(cmd);
}

static void esegui(PhTrasferimenti *t, V *v, PhAdb *adb)
{
    PhErrore e = {{0}};
    PhFileEsito es = PH_FILE_RIUSCITO;
    bool creato = false;

    if (v->verso == PH_TR_RICEZIONE) {
        PhFileVoce info;
        es = ph_file_info(adb, v->origine, &info, &e);
        if (es == PH_FILE_RIUSCITO && info.cartella) {
            ph_errore(&e, "%s e' una cartella: si possono scaricare solo i file", v->origine);
            es = PH_FILE_ERRORE;
        } else if (es == PH_FILE_RIUSCITO) {
            pthread_mutex_lock(&t->m);
            v->dimensione = info.dimensione;
            pthread_mutex_unlock(&t->m);
        }
    }
    for (int tentativo = 0; es == PH_FILE_RIUSCITO; tentativo++) {
        es = scegli_destinazione(t, v, adb, &creato, &e);
        if (es != PH_FILE_RIUSCITO)
            break;
        pthread_mutex_lock(&t->m);
        avvisa(t, v); /* ora in corso, con la destinazione definitiva */
        pthread_mutex_unlock(&t->m);
        Ctx c = {t, v};
        if (v->verso == PH_TR_INVIO) {
            es = ph_file_invia(adb, v->origine, v->destinazione, progresso, &c, &t->annulla, &e);
            break;
        }
        es = ph_file_ricevi(adb, v->origine, v->destinazione, progresso, &c, &t->annulla, &e);
        if (es != PH_FILE_ESISTE || tentativo >= 20)
            break;
        /* Dopo la scelta e' comparso qualcosa (il ".parziale" o un file al
         * posto della prenotazione): ph_file_ricevi non ha toccato nulla.
         * Si toglie la prenotazione (se e' ancora la nostra) e si riprova col
         * numero successivo. */
        if (creato)
            togli_prenotazione(v);
        creato = false;
        e.testo[0] = '\0';
        v->indice++;
        es = PH_FILE_RIUSCITO;
    }
    if (creato && es != PH_FILE_RIUSCITO)
        togli_prenotazione(v);

    pthread_mutex_lock(&t->m);
    char *dest_scanner = NULL;
    bool fermata = false;
    bool annullato = atomic_load(&t->annulla);
    if (es == PH_FILE_RIUSCITO) {
        v->stato = PH_TR_COMPLETATO;
        if (v->dimensione < v->fatti)
            v->dimensione = v->fatti;
        v->fatti = v->dimensione;
        if (v->verso == PH_TR_INVIO && !t->chiudi)
            dest_scanner = dup_s(v->destinazione);
    } else if (es == PH_FILE_ANNULLATO || annullato) {
        v->stato = PH_TR_ANNULLATO;
    } else {
        const char *frase = e.testo[0] ? e.testo
                          : es == PH_FILE_NON_TROVATO ? "File o cartella non trovati sul telefono"
                          : es == PH_FILE_NON_CONSENTITO ? "Accesso non consentito dal sistema"
                          : "Trasferimento non riuscito";
        char testo[sizeof e.testo + 64];
        bool morta = !ph_adb_viva(adb);
        if (morta) {
            snprintf(testo, sizeof testo, "Connessione con il telefono persa: %s", frase);
            if (t->adb == adb) {
                t->ferma = true;
                fermata = true;
            }
        } else {
            snprintf(testo, sizeof testo, "%s", frase);
        }
        char *nuovo = dup_s(testo);
        if (nuovo) {
            free(v->errore);
            v->errore = nuovo;
        }
        v->stato = PH_TR_FALLITO;
    }
    t->corrente = NULL;
    if (!dest_scanner) {
        /* niente scanner: la connessione non serve piu', si rilascia prima
         * dell'avviso (cosi' chi riprende dall'avviso della coda non aspetta) */
        t->uso = false;
        pthread_cond_broadcast(&t->c);
    }
    avvisa(t, v); /* v puo' sparire da qui in poi */
    if (fermata)
        avvisa_coda(t);
    pthread_mutex_unlock(&t->m);

    if (dest_scanner) {
        avvisa_scanner(adb, dest_scanner);
        free(dest_scanner);
    }
}

static V *prima_attesa(PhTrasferimenti *t)
{
    for (size_t i = 0; i < t->numero; i++)
        if (t->voci[i]->stato == PH_TR_ATTESA)
            return t->voci[i];
    return NULL;
}

static void *lavoro(void *arg)
{
    PhTrasferimenti *t = arg;
    pthread_mutex_lock(&t->m);
    t->worker = pthread_self();
    t->worker_noto = true;
    while (!t->chiudi) {
        V *v = t->ferma ? NULL : prima_attesa(t);
        if (!v) {
            pthread_cond_wait(&t->c, &t->m);
            continue;
        }
        if (!t->adb || !ph_adb_viva(t->adb)) {
            t->ferma = true; /* la voce resta in attesa */
            avvisa_coda(t);
            continue;
        }
        PhAdb *adb = t->adb;
        t->uso = true;
        atomic_store(&t->annulla, false);
        v->stato = PH_TR_CORSO;
        t->corrente = v;
        pthread_mutex_unlock(&t->m);
        esegui(t, v, adb); /* compreso lo scanner: adb non serve piu' dopo */
        pthread_mutex_lock(&t->m);
        t->uso = false;
        pthread_cond_broadcast(&t->c);
    }
    pthread_mutex_unlock(&t->m);
    return NULL;
}

/* ---- Interfaccia ----------------------------------------------------------- */

PhTrasferimenti *ph_tr_crea(PhAdb *adb, PhTrAvviso avviso, void *utente, PhErrore *e)
{
    if (!adb) {
        ph_errore(e, "coda dei trasferimenti: manca la connessione");
        return NULL;
    }
    PhTrasferimenti *t = calloc(1, sizeof *t);
    if (!t) {
        ph_errore(e, "coda dei trasferimenti: memoria esaurita");
        return NULL;
    }
    t->adb = adb;
    t->avviso = avviso;
    t->utente = utente;
    atomic_init(&t->annulla, false);
    if (pthread_mutex_init(&t->m, NULL) != 0 || pthread_cond_init(&t->c, NULL) != 0) {
        free(t);
        ph_errore(e, "coda dei trasferimenti: impossibile creare il blocco");
        return NULL;
    }
    if (pthread_create(&t->thread, NULL, lavoro, t) != 0) {
        pthread_cond_destroy(&t->c);
        pthread_mutex_destroy(&t->m);
        free(t);
        ph_errore(e, "coda dei trasferimenti: impossibile avviare il thread");
        return NULL;
    }
    t->thread_vivo = true;
    return t;
}

void ph_tr_chiudi(PhTrasferimenti *t)
{
    if (!t)
        return;
    pthread_mutex_lock(&t->m);
    t->chiudi = true;
    atomic_store(&t->annulla, true);
    pthread_cond_broadcast(&t->c);
    pthread_mutex_unlock(&t->m);
    if (t->thread_vivo)
        pthread_join(t->thread, NULL);
    for (size_t i = 0; i < t->numero; i++)
        v_libera(t->voci[i]);
    free(t->voci);
    pthread_cond_destroy(&t->c);
    pthread_mutex_destroy(&t->m);
    free(t);
}

int ph_tr_accoda(PhTrasferimenti *t, PhTrVerso verso, const char *origine, const char *cartella,
                 uint64_t *id, PhErrore *e)
{
    if (!t || !origine || !cartella || !origine[0] || !cartella[0])
        return ph_errore(e, "accodamento: argomenti mancanti");
    char orig[PERCORSO_BUF], cart[PERCORSO_BUF];
    uint64_t dim = 0;
    struct stat st;
    if (verso == PH_TR_INVIO) {
        if (stat(origine, &st) != 0)
            return ph_errore(e, "%s: %s", origine, strerror(errno));
        if (!S_ISREG(st.st_mode))
            return ph_errore(e, "%s non e' un file: si possono inviare solo i file", origine);
        dim = (uint64_t)st.st_size;
        if (strlen(origine) >= sizeof orig)
            return ph_errore(e, "Percorso troppo lungo: %s", origine);
        strcpy(orig, origine);
        if (ph_tr_percorso_telefono(cartella, cart, sizeof cart, e) != 0)
            return -1;
    } else if (verso == PH_TR_RICEZIONE) {
        if (ph_tr_percorso_telefono(origine, orig, sizeof orig, e) != 0)
            return -1;
        if (stat(cartella, &st) != 0 || !S_ISDIR(st.st_mode))
            return ph_errore(e, "La cartella di destinazione sul PC non esiste: %s", cartella);
        if (strlen(cartella) >= sizeof cart)
            return ph_errore(e, "Percorso troppo lungo: %s", cartella);
        strcpy(cart, cartella);
    } else {
        return ph_errore(e, "accodamento: verso sconosciuto");
    }
    const char *nome = ultimo_pezzo(orig);
    if (nome[0] == '\0' || strlen(nome) > nome_massimo(verso) || strcmp(nome, ".") == 0 || strcmp(nome, "..") == 0)
        return ph_errore(e, "Nome di file non utilizzabile: %s", orig);

    V *v = calloc(1, sizeof *v);
    if (!v)
        return ph_errore(e, "Memoria esaurita");
    v->verso = verso;
    v->stato = PH_TR_ATTESA;
    v->base = dup_s(nome);
    v->nome = dup_s(nome);
    v->origine = dup_s(orig);
    v->cartella = dup_s(cart);
    v->errore = dup_s("");
    v->dimensione = dim;
    if (!v->base || !v->nome || !v->origine || !v->cartella || !v->errore) {
        v_libera(v);
        return ph_errore(e, "Memoria esaurita");
    }

    pthread_mutex_lock(&t->m);
    if (t->chiudi) {
        pthread_mutex_unlock(&t->m);
        v_libera(v);
        return ph_errore(e, "La coda dei trasferimenti e' chiusa");
    }
    /* Nome provvisorio: libero oggi e diverso da quello delle altre voci vive.
     * Sul telefono non si interroga qui (sarebbe una chiamata di rete): lo fa
     * il thread di lavoro alla partenza. */
    bool trovato = false;
    for (unsigned n = 0; n < TENTATIVI_NOME && !trovato; n++) {
        char nn[NOME_MAX_BYTE + 1];
        if (nome_indice(v->base, n, nn, sizeof nn, nome_massimo(verso)) != 0)
            break;
        char *dest = unisci(v->cartella, nn);
        if (!dest)
            break;
        bool occupato = riservato(t, v, dest) || (verso == PH_TR_RICEZIONE && pc_occupato(dest));
        if (occupato) {
            free(dest);
            continue;
        }
        char *n2 = dup_s(nn);
        if (!n2) {
            free(dest);
            break;
        }
        free(v->nome);
        v->nome = n2;
        v->destinazione = dest;
        v->indice = n;
        trovato = true;
    }
    if (!trovato || t->numero == SIZE_MAX) {
        pthread_mutex_unlock(&t->m);
        v_libera(v);
        return ph_errore(e, "Nessun nome libero trovato per %s", nome);
    }
    if (t->numero == t->capacita) {
        size_t nc = t->capacita ? t->capacita * 2 : 16;
        V **nv = realloc(t->voci, nc * sizeof *nv);
        if (!nv) {
            pthread_mutex_unlock(&t->m);
            v_libera(v);
            return ph_errore(e, "Memoria esaurita");
        }
        t->voci = nv;
        t->capacita = nc;
    }
    v->id = ++t->prossimo_id;
    t->voci[t->numero++] = v;
    if (id)
        *id = v->id;
    pthread_cond_broadcast(&t->c);
    pthread_mutex_unlock(&t->m);
    return 0;
}

int ph_tr_annulla(PhTrasferimenti *t, uint64_t id, PhErrore *e)
{
    if (!t)
        return ph_errore(e, "annullamento: coda mancante");
    int r = -1;
    pthread_mutex_lock(&t->m);
    for (size_t i = 0; i < t->numero; i++) {
        V *v = t->voci[i];
        if (v->id != id)
            continue;
        if (v->stato == PH_TR_ATTESA) {
            v->stato = PH_TR_ANNULLATO;
            r = 0;
        } else if (v->stato == PH_TR_CORSO && v == t->corrente) {
            atomic_store(&t->annulla, true);
            r = 0;
        }
        break;
    }
    pthread_mutex_unlock(&t->m);
    if (r != 0)
        ph_errore(e, "Il trasferimento %llu non e' piu' annullabile", (unsigned long long)id);
    return r;
}

static void annulla_locked(PhTrasferimenti *t)
{
    for (size_t i = 0; i < t->numero; i++)
        if (t->voci[i]->stato == PH_TR_ATTESA)
            t->voci[i]->stato = PH_TR_ANNULLATO;
    if (t->corrente)
        atomic_store(&t->annulla, true);
}

void ph_tr_annulla_tutto(PhTrasferimenti *t)
{
    if (!t)
        return;
    pthread_mutex_lock(&t->m);
    annulla_locked(t);
    pthread_mutex_unlock(&t->m);
}

int ph_tr_elenca(PhTrasferimenti *t, PhTrElenco *elenco, PhErrore *e)
{
    if (!t || !elenco)
        return ph_errore(e, "elenco dei trasferimenti: argomenti mancanti");
    elenco->voci = NULL;
    elenco->numero = 0;
    pthread_mutex_lock(&t->m);
    size_t n = t->numero;
    PhTrVoce *a = n ? calloc(n, sizeof *a) : NULL;
    if (n && !a) {
        pthread_mutex_unlock(&t->m);
        return ph_errore(e, "Memoria esaurita");
    }
    for (size_t i = 0; i < n; i++) {
        if (voce_copia(t->voci[i], &a[i]) != 0) {
            for (size_t j = 0; j < i; j++)
                voce_pub_libera(&a[j]);
            free(a);
            pthread_mutex_unlock(&t->m);
            return ph_errore(e, "Memoria esaurita");
        }
    }
    pthread_mutex_unlock(&t->m);
    elenco->voci = a;
    elenco->numero = n;
    return 0;
}

void ph_tr_elenco_libera(PhTrElenco *elenco)
{
    if (!elenco)
        return;
    for (size_t i = 0; i < elenco->numero; i++)
        voce_pub_libera(&elenco->voci[i]);
    free(elenco->voci);
    elenco->voci = NULL;
    elenco->numero = 0;
}

bool ph_tr_in_corso(PhTrasferimenti *t)
{
    if (!t)
        return false;
    bool r = false;
    pthread_mutex_lock(&t->m);
    for (size_t i = 0; i < t->numero && !r; i++) {
        PhTrStato s = t->voci[i]->stato;
        r = s == PH_TR_CORSO || (s == PH_TR_ATTESA && !t->ferma);
    }
    pthread_mutex_unlock(&t->m);
    return r;
}

bool ph_tr_coda_ferma(PhTrasferimenti *t)
{
    if (!t)
        return false;
    pthread_mutex_lock(&t->m);
    bool r = t->ferma;
    pthread_mutex_unlock(&t->m);
    return r;
}

/* true se chiamata dal thread di lavoro (cioe' da un avviso): aspettare se
 * stesso sarebbe uno stallo. Mutex preso. */
static bool dal_lavoro(const PhTrasferimenti *t)
{
    return t->worker_noto && pthread_equal(pthread_self(), t->worker);
}

int ph_tr_riprendi(PhTrasferimenti *t, PhAdb *nuova, PhErrore *e)
{
    if (!t)
        return ph_errore(e, "riprendi: coda mancante");
    pthread_mutex_lock(&t->m);
    if (dal_lavoro(t)) {
        pthread_mutex_unlock(&t->m);
        return ph_errore(e, "ph_tr_riprendi non si puo' chiamare dall'avviso della coda");
    }
    while (t->uso && !t->chiudi)
        pthread_cond_wait(&t->c, &t->m);
    t->adb = nuova;
    t->ferma = nuova == NULL;
    pthread_cond_broadcast(&t->c);
    pthread_mutex_unlock(&t->m);
    return 0;
}

/* Mutex preso: toglie le voci terminate (la voce in corso non e' terminata). */
static void rimuovi_locked(PhTrasferimenti *t)
{
    size_t k = 0;
    for (size_t i = 0; i < t->numero; i++) {
        if (terminato(t->voci[i]->stato))
            v_libera(t->voci[i]);
        else
            t->voci[k++] = t->voci[i];
    }
    t->numero = k;
}

int ph_tr_svuota(PhTrasferimenti *t, PhErrore *e)
{
    if (!t)
        return ph_errore(e, "svuota: coda mancante");
    pthread_mutex_lock(&t->m);
    if (dal_lavoro(t)) {
        pthread_mutex_unlock(&t->m);
        return ph_errore(e, "ph_tr_svuota non si puo' chiamare dall'avviso della coda");
    }
    annulla_locked(t);
    rimuovi_locked(t);
    if (t->ferma) {
        /* chi sta sopra ha gia' chiuso (o sta per chiudere) la connessione
         * caduta: la coda la lascia, dopo che il thread ha finito di usarla */
        while (t->uso && !t->chiudi)
            pthread_cond_wait(&t->c, &t->m);
        t->adb = NULL;
    }
    pthread_cond_broadcast(&t->c);
    pthread_mutex_unlock(&t->m);
    return 0;
}

void ph_tr_rimuovi_finiti(PhTrasferimenti *t)
{
    if (!t)
        return;
    pthread_mutex_lock(&t->m);
    rimuovi_locked(t);
    pthread_mutex_unlock(&t->m);
}
