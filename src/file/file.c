/* Copia di file ed elenco cartelle con il servizio "sync:" di ADB.
 * Protocollo: richieste di 4 lettere + lunghezza a 32 bit little endian
 * (documentato in AOSP, SYNC.TXT; qui scritto da zero). Si usano le varianti
 * v2 (STA2, LST2, LIS2) perche' portano dimensioni a 64 bit ed errori
 * distinti (errno del telefono). */
#include "file/file.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

/* Quanto aspettare, senza ricevere nulla, prima di dichiarare il telefono
 * bloccato. Le letture sono a fette brevi, cosi' *annulla si controlla spesso.
 * I test compilano con valori piccoli. */
#ifndef PH_FILE_ATTESA_MS
#define PH_FILE_ATTESA_MS 30000
#endif
#ifndef PH_FILE_FETTA_MS
#define PH_FILE_FETTA_MS 200
#endif

#define BLOCCO_MAX 65536     /* massimo di un blocco DATA (limite del protocollo) */
#define PERCORSO_MAX 1024    /* massimo di un percorso in una richiesta sync */
#define NOME_MAX_VOCE 1024   /* un nome piu' lungo e' una risposta malformata */
#define MESSAGGIO_MAX 4096   /* massimo di un messaggio FAIL */
#define VOCI_MAX 200000      /* tetti di sicurezza: voci di una cartella... */
#define NOMI_BYTE_MAX (64u * 1024 * 1024) /* ...e byte totali dei loro nomi */
/* Blocchi DATA (da 64 KiB, limite del protocollo sync) raccolti in una sola
 * scrittura sul canale: una scrittura = un giro di conferme in meno per blocco.
 * L'annullamento si controlla a ogni scrittura (circa 1 MiB). */
#define BLOCCHI_PER_SCRITTURA 16
#define MODO_NUOVO_FILE 33188 /* 0100644 in decimale: file normale rw-r--r-- */

typedef struct {
    PhCanale *c;
    const atomic_bool *annulla; /* NULL = non controllare (fase di pulizia) */
    PhErrore *e;
} Sync;

typedef struct {
    uint32_t errore; /* errno del telefono, 0 se la risposta era valida */
    uint32_t modo;
    uint64_t dimensione;
    int64_t modificato;
} Stat;

/* ---- Utilita' ------------------------------------------------------------ */

static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint64_t le64(const uint8_t *p)
{
    return (uint64_t)le32(p) | ((uint64_t)le32(p + 4) << 32);
}

static void metti32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static bool annullato(const Sync *s)
{
    return s->annulla != NULL && atomic_load(s->annulla);
}

static PhFileEsito esito_da_errno(uint32_t err)
{
    switch (err) {
    case ENOENT:
    case ENOTDIR:
        return PH_FILE_NON_TROVATO;
    case EACCES:
    case EPERM:
        return PH_FILE_NON_CONSENTITO;
    default:
        return PH_FILE_ERRORE;
    }
}

/* Il FAIL del telefono e' solo testo: lo si classifica dalle parole che
 * Android usa (strerror) o dal nome dell'errno. */
static PhFileEsito esito_da_messaggio(const char *m)
{
    if (strcasestr(m, "No such file") || strstr(m, "ENOENT") || strcasestr(m, "Not a directory") ||
        strstr(m, "ENOTDIR"))
        return PH_FILE_NON_TROVATO;
    if (strcasestr(m, "Permission denied") || strstr(m, "EACCES") || strcasestr(m, "Operation not permitted") ||
        strstr(m, "EPERM"))
        return PH_FILE_NON_CONSENTITO;
    return PH_FILE_ERRORE;
}

static PhFileEsito scrivi_tutto(Sync *s, const void *dati, size_t n)
{
    return ph_canale_scrivi(s->c, dati, n, s->e) == 0 ? PH_FILE_RIUSCITO : PH_FILE_ERRORE;
}

/* Legge esattamente n byte. Un telefono che tace per troppo tempo o che
 * chiude a meta' e' un errore, mai un'attesa infinita. */
static PhFileEsito leggi_esatto(Sync *s, void *dati, size_t n)
{
    uint8_t *p = dati;
    size_t fatti = 0;
    int fermo = 0;
    while (fatti < n) {
        if (annullato(s))
            return PH_FILE_ANNULLATO;
        ssize_t r = ph_canale_leggi(s->c, p + fatti, n - fatti, PH_FILE_FETTA_MS, s->e);
        if (r > 0) {
            if ((size_t)r > n - fatti) {
                ph_errore(s->e, "lettura dal canale: restituiti piu' byte di quelli chiesti");
                return PH_FILE_ERRORE;
            }
            fatti += (size_t)r;
            fermo = 0;
        } else if (r == PH_CANALE_TEMPO) {
            fermo += PH_FILE_FETTA_MS;
            if (fermo >= PH_FILE_ATTESA_MS) {
                ph_errore(s->e, "il telefono non risponde piu'");
                return PH_FILE_ERRORE;
            }
        } else if (r == PH_CANALE_CHIUSO) {
            ph_errore(s->e, "il telefono ha chiuso il canale prima della fine della risposta");
            return PH_FILE_ERRORE;
        } else {
            return PH_FILE_ERRORE; /* PhErrore gia' riempito dal canale */
        }
    }
    return PH_FILE_RIUSCITO;
}

static PhFileEsito invia_richiesta(Sync *s, const char id[4], const void *dati, size_t n)
{
    uint8_t *b = malloc(8 + n);
    if (!b) {
        ph_errore(s->e, "memoria esaurita");
        return PH_FILE_ERRORE;
    }
    memcpy(b, id, 4);
    metti32(b + 4, (uint32_t)n);
    if (n > 0)
        memcpy(b + 8, dati, n);
    PhFileEsito r = scrivi_tutto(s, b, 8 + n);
    free(b);
    return r;
}

static void sync_quit(Sync *s)
{
    static const uint8_t quit[8] = {'Q', 'U', 'I', 'T', 0, 0, 0, 0};
    (void)ph_canale_scrivi(s->c, quit, sizeof quit, NULL);
}

/* Dopo un id "FAIL" legge lunghezza e messaggio e li trasforma in esito. */
static PhFileEsito leggi_fail(Sync *s)
{
    uint8_t l[4];
    PhFileEsito r = leggi_esatto(s, l, 4);
    if (r != PH_FILE_RIUSCITO)
        return r;
    uint32_t n = le32(l);
    if (n > MESSAGGIO_MAX) {
        ph_errore(s->e, "risposta malformata del telefono (messaggio di errore troppo lungo)");
        return PH_FILE_ERRORE;
    }
    char m[MESSAGGIO_MAX + 1];
    r = leggi_esatto(s, m, n);
    if (r != PH_FILE_RIUSCITO)
        return r;
    m[n] = '\0';
    for (uint32_t i = 0; i < n; i++)
        if ((unsigned char)m[i] < 0x20)
            m[i] = ' ';
    ph_errore(s->e, "il telefono ha risposto: %s", n ? m : "errore senza messaggio");
    return esito_da_messaggio(m);
}

static PhCanale *apri_sync(PhAdb *a, PhErrore *e)
{
    PhCanale *c = ph_canale_apri(a, "sync:", 10000, e);
    if (!c)
        ph_errore_contesto(e, "apertura del servizio sync");
    return c;
}

/* STA2 (segue i link) o LST2 (non li segue): 'id' dice quale. */
static PhFileEsito sync_stat(Sync *s, const char *id, const char *percorso, Stat *st)
{
    size_t lun = strlen(percorso);
    if (lun == 0 || lun > PERCORSO_MAX)
        return ph_errore(s->e, "percorso del telefono vuoto o troppo lungo"), PH_FILE_ERRORE;
    PhFileEsito r = invia_richiesta(s, id, percorso, lun);
    if (r != PH_FILE_RIUSCITO)
        return r;
    uint8_t risp[4 + 68];
    r = leggi_esatto(s, risp, 4);
    if (r != PH_FILE_RIUSCITO)
        return r;
    if (memcmp(risp, "FAIL", 4) == 0)
        return leggi_fail(s);
    if (memcmp(risp, id, 4) != 0)
        return ph_errore(s->e, "risposta inattesa del telefono alle informazioni su un file"), PH_FILE_ERRORE;
    r = leggi_esatto(s, risp + 4, 68);
    if (r != PH_FILE_RIUSCITO)
        return r;
    const uint8_t *d = risp + 4;
    uint32_t err = le32(d);
    if (err != 0) {
        st->errore = err;
        char tmp[128];
        ph_errore(s->e, "%s: %s", percorso, strerror_r((int)err, tmp, sizeof tmp));
        return esito_da_errno(err);
    }
    st->modo = le32(d + 20);
    st->dimensione = le64(d + 36);
    st->modificato = (int64_t)le64(d + 52);
    return PH_FILE_RIUSCITO;
}

/* ---- Invio ---------------------------------------------------------------- */

/* Toglie un file dal telefono. Il servizio sync non sa cancellare: si usa una
 * shell, con il percorso tra apici (un apice dentro diventa '\''). */
static void rimuovi_remoto(PhAdb *a, const char *remoto)
{
    size_t lun = strlen(remoto);
    char *cmd = malloc(32 + 4 * lun);
    if (!cmd)
        return;
    char *p = cmd + sprintf(cmd, "shell:rm -f -- '");
    for (size_t i = 0; i < lun; i++) {
        if (remoto[i] == '\'') {
            memcpy(p, "'\\''", 4);
            p += 4;
        } else {
            *p++ = remoto[i];
        }
    }
    *p++ = '\'';
    *p = '\0';
    PhCanale *c = ph_canale_apri(a, cmd, 5000, NULL);
    free(cmd);
    if (!c)
        return;
    /* Si aspetta la fine del comando (canale chiuso), con un tetto. */
    char tmp[256];
    for (int i = 0; i < 10; i++) {
        ssize_t r = ph_canale_leggi(c, tmp, sizeof tmp, 500, NULL);
        if (r == PH_CANALE_CHIUSO || r == PH_CANALE_ERRORE)
            break;
    }
    ph_canale_chiudi(c);
}

/* Lettura del file locale. Nella libreria di prova (macro di compilazione)
 * restituisce meno byte di quelli chiesti, con lunghezze che variano, come
 * fanno file su rete o FUSE: serve a provare che la raccolta dei blocchi regge. */
static ssize_t leggi_locale(int fd, void *dati, size_t n)
{
#ifdef PH_FILE_PROVA_LETTURE_CORTE
    static unsigned long x = 12345;
    x = x * 1103515245UL + 12345UL;
    size_t corta = 1 + (x >> 8) % 70000;
    if (corta < n)
        n = corta;
#endif
    return read(fd, dati, n);
}

/* Limite noto: l'annullamento si vede a ogni blocco scritto, ma ph_canale_scrivi
 * puo' restare bloccato se il telefono smette di confermare (controllo di
 * flusso) senza far cadere la connessione: in quel caso l'invio non vede
 * *annulla finche' la scrittura non torna. E' un limite del contratto adb. */
PhFileEsito ph_file_invia(PhAdb *a, const char *locale, const char *remoto, PhFileProgresso progresso,
                          void *utente, const atomic_bool *annulla, PhErrore *e)
{
    if (!a || !locale || !remoto)
        return ph_errore(e, "invio: argomenti mancanti"), PH_FILE_ERRORE;
    /* adbd accetta al massimo 1024 byte per l'intera richiesta "percorso,modo". */
    char req[PERCORSO_MAX + 16];
    int lr = snprintf(req, sizeof req, "%s,%d", remoto, MODO_NUOVO_FILE);
    if (strlen(remoto) == 0 || lr < 0 || (size_t)lr > PERCORSO_MAX)
        return ph_errore(e, "invio: percorso del telefono vuoto o troppo lungo"), PH_FILE_ERRORE;

    int fd = open(locale, O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        return ph_errore(e, "apertura di %s: %s", locale, strerror(errno)), PH_FILE_ERRORE;
    struct stat sb;
    if (fstat(fd, &sb) != 0 || !S_ISREG(sb.st_mode)) {
        ph_errore(e, "%s non e' un file normale", locale);
        close(fd);
        return PH_FILE_ERRORE;
    }
    uint8_t *buf = malloc((size_t)(8 + BLOCCO_MAX) * BLOCCHI_PER_SCRITTURA);
    if (!buf) {
        close(fd);
        return ph_errore(e, "memoria esaurita"), PH_FILE_ERRORE;
    }
    PhCanale *c = apri_sync(a, e);
    if (!c) {
        free(buf);
        close(fd);
        return PH_FILE_ERRORE;
    }
    Sync s = {c, annulla, e};
    uint64_t totale = (uint64_t)sb.st_size, fatti = 0;

    PhFileEsito esito = invia_richiesta(&s, "SEND", req, strlen(req));
    bool canale_rotto = esito != PH_FILE_RIUSCITO;
    if (esito == PH_FILE_RIUSCITO && progresso)
        progresso(0, totale, utente);

    while (esito == PH_FILE_RIUSCITO) {
        if (annullato(&s)) {
            esito = PH_FILE_ANNULLATO;
            break;
        }
        size_t usato = 0;
        uint64_t aggiunti = 0;
        bool fine_file = false;
        /* Un blocco intero deve sempre starci: dopo letture corte `usato` non e'
         * un multiplo pieno, quindi si guarda lo spazio che resta davvero. */
        while (usato + 8 + BLOCCO_MAX <= (size_t)(8 + BLOCCO_MAX) * BLOCCHI_PER_SCRITTURA && !fine_file) {
            ssize_t r = leggi_locale(fd, buf + usato + 8, BLOCCO_MAX);
            if (r < 0) {
                if (errno == EINTR)
                    continue;
                ph_errore(e, "lettura di %s: %s", locale, strerror(errno));
                esito = PH_FILE_ERRORE;
                break;
            }
            if (r == 0) {
                fine_file = true;
                break;
            }
            memcpy(buf + usato, "DATA", 4);
            metti32(buf + usato + 4, (uint32_t)r);
            usato += 8 + (size_t)r;
            aggiunti += (uint64_t)r;
        }
        /* Quanto letto prima di un eventuale errore locale va comunque inviato. */
        PhErrore causa_prima = {{0}};
        bool errore_locale = esito != PH_FILE_RIUSCITO;
        if (errore_locale && e)
            causa_prima = *e;
        if (usato > 0 && scrivi_tutto(&s, buf, usato) != PH_FILE_RIUSCITO) {
            esito = PH_FILE_ERRORE;
            canale_rotto = true;
            if (errore_locale && e)
                *e = causa_prima; /* la causa prima e' l'errore di lettura locale */
            break;
        }
        fatti += aggiunti;
        if (usato > 0 && progresso)
            progresso(fatti, totale > fatti ? totale : fatti, utente);
        if (fine_file)
            break;
    }

    /* Da qui in poi si chiude comunque in modo ordinato (DONE e risposta),
     * anche se annullati: cosi' il telefono ha finito col file prima che lo
     * si cancelli, e non resta una corsa tra la chiusura e la cancellazione. */
    s.annulla = NULL;
    PhFileEsito risposta = PH_FILE_ERRORE;
    bool fail_ricevuto = false; /* il telefono ha risposto FAIL: il suo parziale l'ha tolto lui */
    PhErrore salvato = {{0}};
    if (e)
        salvato = *e;
    if (!canale_rotto) {
        int64_t m = sb.st_mtime;
        uint8_t done[8] = {'D', 'O', 'N', 'E'};
        metti32(done + 4, m < 0 ? 0 : (m > (int64_t)UINT32_MAX ? UINT32_MAX : (uint32_t)m));
        risposta = scrivi_tutto(&s, done, sizeof done);
    }
    if (risposta == PH_FILE_RIUSCITO || canale_rotto) {
        uint8_t id[4];
        PhFileEsito r = leggi_esatto(&s, id, 4);
        if (r == PH_FILE_RIUSCITO && memcmp(id, "OKAY", 4) == 0) {
            uint8_t l[4];
            risposta = leggi_esatto(&s, l, 4); /* lunghezza, di solito 0 */
        } else if (r == PH_FILE_RIUSCITO && memcmp(id, "FAIL", 4) == 0) {
            fail_ricevuto = true;
            risposta = leggi_fail(&s);
            if (risposta == PH_FILE_RIUSCITO) /* leggi_fail non torna mai riuscito */
                risposta = PH_FILE_ERRORE;
        } else if (r == PH_FILE_RIUSCITO) {
            ph_errore(e, "risposta inattesa del telefono alla fine dell'invio");
            risposta = PH_FILE_ERRORE;
        } else {
            risposta = r;
        }
    }
    if (esito == PH_FILE_RIUSCITO) {
        esito = risposta;
    } else if (canale_rotto && (risposta == PH_FILE_NON_CONSENTITO || risposta == PH_FILE_NON_TROVATO)) {
        esito = risposta; /* il telefono ci dice il motivo vero del guasto */
    } else if (e) {
        *e = salvato; /* la lettura della risposta puo' aver sovrascritto il primo guasto */
    }
    if (!canale_rotto)
        sync_quit(&s);
    ph_canale_chiudi(c);

    /* Il parziale remoto si toglie solo per guasti dal nostro lato (annullato,
     * errore di lettura locale, canale rotto senza una risposta). Se il
     * telefono ha risposto FAIL, il suo file l'ha gia' gestito lui: un nostro
     * rm cancellerebbe un file preesistente quando l'apertura e' fallita per
     * altri motivi. */
    if (esito != PH_FILE_RIUSCITO && !fail_ricevuto)
        rimuovi_remoto(a, remoto);

    free(buf);
    close(fd);
    return esito;
}

/* ---- Ricezione ------------------------------------------------------------ */

static int scrivi_locale(int fd, const uint8_t *d, size_t n, PhErrore *e)
{
    size_t fatti = 0;
    while (fatti < n) {
        ssize_t r = write(fd, d + fatti, n - fatti);
        if (r < 0) {
            if (errno == EINTR)
                continue;
            return ph_errore(e, "scrittura del file locale: %s", strerror(errno));
        }
        fatti += (size_t)r;
    }
    return 0;
}

/* Sposta il parziale al nome finale senza mai sovrascrivere un file dell'utente.
 * Unica eccezione: un file normale di 0 byte, che la coda dei trasferimenti crea
 * come prenotazione del nome prima di chiamare ph_file_ricevi; quello si
 * sostituisce con un rename normale. Altrimenti RENAME_NOREPLACE (con ripiego
 * link+unlink se il file system non lo supporta). I nomi passati devono
 * venire dalla coda: un file vuoto dell'utente con quel nome verrebbe
 * sostituito. Un ".parziale" rimasto da un crash rende il nome occupato per
 * sempre (PH_FILE_ESISTE): chi chiama ne sceglie un altro. Se nel frattempo e' comparso
 * qualcosa, PH_FILE_ESISTE e il file dell'utente resta intatto. */
#ifdef PH_FILE_PROVA_GANCI
/* Solo nelle librerie di prova: il test puo' simulare la corsa (numero 1: prima
 * del primo scambio compare qualcosa al posto della prenotazione) e il
 * fallimento di uno scambio (se torna != 0). */
extern int (*ph_file_prova_scambio)(int numero, const char *locale);
int (*ph_file_prova_scambio)(int numero, const char *locale) = NULL;
#endif

static int scambia_nomi(const char *a, const char *b, int numero)
{
#ifdef PH_FILE_PROVA_GANCI
    if (ph_file_prova_scambio && ph_file_prova_scambio(numero, b) != 0) {
        errno = EIO;
        return -1;
    }
#else
    (void)numero;
#endif
    return renameat2(AT_FDCWD, a, AT_FDCWD, b, RENAME_EXCHANGE);
}

static PhFileEsito sposta_parziale(const char *parziale, const char *locale, bool *tieni_parziale, PhErrore *e)
{
    for (int tentativo = 0; tentativo < 3; tentativo++) {
        struct stat sb;
        if (lstat(locale, &sb) == 0) {
            if (!(S_ISREG(sb.st_mode) && sb.st_size == 0))
                break; /* esiste e non e' una prenotazione */
            /* Prenotazione: la si scambia col parziale e si guarda cosa e'
             * finito al nome del parziale. Se e' ancora un file vuoto lo si
             * cancella; se nel frattempo e' diventato un file dell'utente lo
             * si rimette al suo posto: non viene mai distrutto, al massimo
             * cambia nome per un attimo. */
            if (scambia_nomi(parziale, locale, 1) == 0) {
                struct stat v;
                if (lstat(parziale, &v) == 0 && S_ISREG(v.st_mode) && v.st_size == 0) {
                    unlink(parziale);
                    return PH_FILE_RIUSCITO;
                }
                if (scambia_nomi(parziale, locale, 2) != 0) {
                    /* Al nome del parziale c'e' ora un file dell'UTENTE: chi
                     * chiama non deve cancellarlo. */
                    *tieni_parziale = true;
                    ph_errore(e, "un file dell'utente e' ora in %s e il file ricevuto e' in %s (ripristino fallito: %s)",
                              parziale, locale, strerror(errno));
                    return PH_FILE_ERRORE;
                }
                break;
            }
            if (errno != ENOSYS && errno != EINVAL && errno != EOPNOTSUPP)
                return ph_errore(e, "scambio con %s: %s", locale, strerror(errno)), PH_FILE_ERRORE;
            /* File system senza RENAME_EXCHANGE: rename normale (resta una
             * finestra minima tra il controllo e il rename). */
            if (rename(parziale, locale) == 0)
                return PH_FILE_RIUSCITO;
            return ph_errore(e, "rinomina in %s: %s", locale, strerror(errno)), PH_FILE_ERRORE;
        }
        if (errno != ENOENT)
            return ph_errore(e, "controllo di %s: %s", locale, strerror(errno)), PH_FILE_ERRORE;
        if (renameat2(AT_FDCWD, parziale, AT_FDCWD, locale, RENAME_NOREPLACE) == 0)
            return PH_FILE_RIUSCITO;
        if (errno == EEXIST)
            continue; /* e' comparso ora: si ricontrolla cos'e' */
        if (errno == ENOSYS || errno == EINVAL || errno == EOPNOTSUPP) {
            if (link(parziale, locale) == 0) {
                unlink(parziale);
                return PH_FILE_RIUSCITO;
            }
            if (errno == EEXIST)
                continue;
        }
        return ph_errore(e, "rinomina in %s: %s", locale, strerror(errno)), PH_FILE_ERRORE;
    }
    ph_errore(e, "%s esiste gia' sul PC: non lo sovrascrivo", locale);
    return PH_FILE_ESISTE;
}

/* Telefono -> PC senza mai toccare file dell'utente:
 *  - "<locale>" gia' esistente: accettato solo se e' un file normale di 0 byte
 *    (prenotazione del nome della coda dei trasferimenti), altrimenti
 *    PH_FILE_ESISTE subito, senza contattare il telefono;
 *  - "<locale>.parziale" si crea con O_CREAT|O_EXCL|O_NOFOLLOW, permessi 0600
 *    (l'umask dell'utente si applica come per ogni file nuovo); se esiste, anche un
 *    link simbolico, PH_FILE_ESISTE e non si tocca nulla. */
PhFileEsito ph_file_ricevi(PhAdb *a, const char *remoto, const char *locale, PhFileProgresso progresso,
                           void *utente, const atomic_bool *annulla, PhErrore *e)
{
    if (!a || !locale || !remoto)
        return ph_errore(e, "ricezione: argomenti mancanti"), PH_FILE_ERRORE;
    size_t lp = strlen(locale) + 16;
    char *parziale = malloc(lp);
    uint8_t *buf = malloc(BLOCCO_MAX);
    if (!parziale || !buf) {
        free(parziale);
        free(buf);
        return ph_errore(e, "memoria esaurita"), PH_FILE_ERRORE;
    }
    snprintf(parziale, lp, "%s.parziale", locale);

    struct stat dest;
    if (lstat(locale, &dest) == 0 && !(S_ISREG(dest.st_mode) && dest.st_size == 0)) {
        ph_errore(e, "%s esiste gia' sul PC: non lo sovrascrivo", locale);
        free(parziale);
        free(buf);
        return PH_FILE_ESISTE;
    }
    int fd = open(parziale, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0644);
    if (fd < 0) {
        PhFileEsito r = errno == EEXIST || errno == ELOOP ? PH_FILE_ESISTE : PH_FILE_ERRORE;
        if (r == PH_FILE_ESISTE)
            ph_errore(e, "%s esiste gia' sul PC: non lo tocco", parziale);
        else
            ph_errore(e, "creazione di %s: %s", parziale, strerror(errno));
        free(parziale);
        free(buf);
        return r;
    }

    PhCanale *c = apri_sync(a, e);
    if (!c) {
        close(fd);
        unlink(parziale); /* l'abbiamo creato noi, qui sopra */
        free(parziale);
        free(buf);
        return PH_FILE_ERRORE;
    }
    Sync s = {c, annulla, e};
    Stat st = {0};
    uint64_t fatti = 0;
    bool tieni_parziale = false; /* il nome del parziale ospita un file dell'utente: non cancellare */

    /* Prima le informazioni: dimensione (per il progresso e per accorgersi di
     * un file troncato), data di modifica, e un errore chiaro se manca o e'
     * negato. */
    PhFileEsito esito = sync_stat(&s, "STA2", remoto, &st);
    if (esito == PH_FILE_RIUSCITO && S_ISDIR((mode_t)st.modo)) {
        ph_errore(e, "%s e' una cartella, non un file", remoto);
        esito = PH_FILE_ERRORE;
    }
    if (esito == PH_FILE_RIUSCITO)
        esito = invia_richiesta(&s, "RECV", remoto, strlen(remoto));
    if (esito == PH_FILE_RIUSCITO && progresso)
        progresso(0, st.dimensione, utente);

    while (esito == PH_FILE_RIUSCITO) {
        uint8_t id[4], l[4];
        esito = leggi_esatto(&s, id, 4);
        if (esito != PH_FILE_RIUSCITO)
            break;
        if (memcmp(id, "DONE", 4) == 0) /* il resto del DONE si scarta chiudendo il canale */
            break;
        if (memcmp(id, "FAIL", 4) == 0) {
            esito = leggi_fail(&s);
            if (esito == PH_FILE_RIUSCITO)
                esito = PH_FILE_ERRORE;
            break;
        }
        if (memcmp(id, "DATA", 4) != 0) {
            ph_errore(e, "risposta inattesa del telefono durante la ricezione");
            esito = PH_FILE_ERRORE;
            break;
        }
        esito = leggi_esatto(&s, l, 4);
        if (esito != PH_FILE_RIUSCITO)
            break;
        uint32_t n = le32(l);
        if (n > BLOCCO_MAX) {
            ph_errore(e, "risposta malformata del telefono (blocco di %u byte)", (unsigned)n);
            esito = PH_FILE_ERRORE;
            break;
        }
        esito = leggi_esatto(&s, buf, n);
        if (esito != PH_FILE_RIUSCITO)
            break;
        if (scrivi_locale(fd, buf, n, e) != 0) {
            esito = PH_FILE_ERRORE;
            break;
        }
        fatti += n;
        if (progresso)
            progresso(fatti, st.dimensione > fatti ? st.dimensione : fatti, utente);
    }
    if (esito == PH_FILE_RIUSCITO && fatti != st.dimensione) {
        ph_errore(e, "%s: ricevuti %llu byte su %llu annunciati", remoto, (unsigned long long)fatti,
                  (unsigned long long)st.dimensione);
        esito = PH_FILE_ERRORE;
    }

    s.annulla = NULL;
    sync_quit(&s);
    ph_canale_chiudi(c);

    {
        if (esito == PH_FILE_RIUSCITO) {
            /* Data di modifica sul descrittore, prima di chiudere: niente
             * passaggio per nome. */
            struct timespec t[2];
            t[0].tv_sec = t[1].tv_sec = (time_t)st.modificato;
            t[0].tv_nsec = t[1].tv_nsec = 0;
            if (futimens(fd, t) != 0) {
                ph_errore(e, "data di modifica di %s: %s", parziale, strerror(errno));
                esito = PH_FILE_ERRORE;
            }
        }
        if (close(fd) != 0 && esito == PH_FILE_RIUSCITO) {
            ph_errore(e, "chiusura di %s: %s", parziale, strerror(errno));
            esito = PH_FILE_ERRORE;
        }
        if (esito == PH_FILE_RIUSCITO)
            esito = sposta_parziale(parziale, locale, &tieni_parziale, e);
        if (esito != PH_FILE_RIUSCITO && !tieni_parziale)
            unlink(parziale);
    }
    free(parziale);
    free(buf);
    return esito;
}

/* ---- Elenco e informazioni ------------------------------------------------ */

void ph_file_elenco_libera(PhFileElenco *elenco)
{
    if (!elenco)
        return;
    for (size_t i = 0; i < elenco->numero; i++)
        free(elenco->voci[i].nome);
    free(elenco->voci);
    elenco->voci = NULL;
    elenco->numero = 0;
}

static int confronta_voci(const void *x, const void *y)
{
    const PhFileVoce *a = x, *b = y;
    if (a->cartella != b->cartella)
        return a->cartella ? -1 : 1;
    int c = strcasecmp(a->nome, b->nome);
    return c != 0 ? c : strcmp(a->nome, b->nome);
}

/* I link possono puntare a cartelle (per esempio /sdcard): per mostrarli come
 * cartelle si guarda dove puntano. Canale nuovo, perche' il DONE di LIS2 ha un
 * seguito che non si legge. Un link rotto resta un link. */
static PhFileEsito risolvi_link(PhAdb *a, const char *cartella, PhFileElenco *el, PhErrore *e)
{
    bool serve = false;
    for (size_t i = 0; i < el->numero; i++)
        serve = serve || el->voci[i].collegamento;
    if (!serve)
        return PH_FILE_RIUSCITO;
    PhCanale *c = apri_sync(a, e);
    if (!c)
        return PH_FILE_ERRORE;
    Sync s = {c, NULL, e};
    size_t lc = strlen(cartella);
    while (lc > 1 && cartella[lc - 1] == '/')
        lc--;
    PhFileEsito esito = PH_FILE_RIUSCITO;
    for (size_t i = 0; i < el->numero && esito == PH_FILE_RIUSCITO; i++) {
        PhFileVoce *v = &el->voci[i];
        if (!v->collegamento)
            continue;
        size_t ln = strlen(v->nome);
        if (lc + 1 + ln > PERCORSO_MAX)
            continue;
        char *p = malloc(lc + 1 + ln + 1);
        if (!p) {
            ph_errore(e, "memoria esaurita");
            esito = PH_FILE_ERRORE;
            break;
        }
        /* Cartella senza slash finali + "/" + nome (per "/" lo slash c'e' gia'). */
        size_t k = 0;
        memcpy(p, cartella, lc);
        k = lc;
        if (!(lc == 1 && cartella[0] == '/'))
            p[k++] = '/';
        memcpy(p + k, v->nome, ln + 1);
        Stat st = {0};
        PhFileEsito r = sync_stat(&s, "STA2", p, &st);
        free(p);
        if (r == PH_FILE_RIUSCITO)
            v->cartella = S_ISDIR((mode_t)st.modo);
        else if (st.errore == 0)
            esito = r; /* errore di trasporto, non del link */
    }
    if (esito == PH_FILE_RIUSCITO)
        sync_quit(&s);
    ph_canale_chiudi(c);
    return esito;
}

PhFileEsito ph_file_elenca(PhAdb *a, const char *cartella, PhFileElenco *elenco, PhErrore *e)
{
    if (!elenco)
        return ph_errore(e, "elenco: argomenti mancanti"), PH_FILE_ERRORE;
    elenco->voci = NULL;
    elenco->numero = 0;
    if (!a || !cartella)
        return ph_errore(e, "elenco: argomenti mancanti"), PH_FILE_ERRORE;
    PhCanale *c = apri_sync(a, e);
    if (!c)
        return PH_FILE_ERRORE;
    Sync s = {c, NULL, e};
    size_t cap = 0;
    size_t voci_viste = 0;   /* DNT2 ricevute, anche "." e ".." */
    size_t byte_nomi = 0;

    /* LIS2 su una cartella inesistente o negata risponde solo DONE, quindi
     * "vuota": per distinguere i casi si chiede prima STA2. */
    Stat st = {0};
    PhFileEsito esito = sync_stat(&s, "STA2", cartella, &st);
    if (esito == PH_FILE_RIUSCITO && !S_ISDIR((mode_t)st.modo)) {
        ph_errore(e, "%s non e' una cartella", cartella);
        esito = PH_FILE_ERRORE;
    }
    if (esito == PH_FILE_RIUSCITO)
        esito = invia_richiesta(&s, "LIS2", cartella, strlen(cartella));

    while (esito == PH_FILE_RIUSCITO) {
        uint8_t id[4];
        esito = leggi_esatto(&s, id, 4);
        if (esito != PH_FILE_RIUSCITO)
            break;
        if (memcmp(id, "DONE", 4) == 0)
            break;
        if (memcmp(id, "FAIL", 4) == 0) {
            esito = leggi_fail(&s);
            if (esito == PH_FILE_RIUSCITO)
                esito = PH_FILE_ERRORE;
            break;
        }
        if (memcmp(id, "DNT2", 4) != 0) {
            ph_errore(e, "risposta inattesa del telefono durante l'elenco");
            esito = PH_FILE_ERRORE;
            break;
        }
        uint8_t d[72];
        esito = leggi_esatto(&s, d, sizeof d);
        if (esito != PH_FILE_RIUSCITO)
            break;
        uint32_t nl = le32(d + 68);
        if (nl == 0 || nl > NOME_MAX_VOCE) {
            ph_errore(e, "risposta malformata del telefono (nome di %u byte)", (unsigned)nl);
            esito = PH_FILE_ERRORE;
            break;
        }
        char *nome = malloc((size_t)nl + 1);
        if (!nome) {
            ph_errore(e, "memoria esaurita");
            esito = PH_FILE_ERRORE;
            break;
        }
        esito = leggi_esatto(&s, nome, nl);
        if (esito != PH_FILE_RIUSCITO) {
            free(nome);
            break;
        }
        nome[nl] = '\0';
        if (strlen(nome) != nl || strchr(nome, '/')) {
            free(nome);
            ph_errore(e, "risposta malformata del telefono (nome non valido)");
            esito = PH_FILE_ERRORE;
            break;
        }
        /* Voci con errore (non leggibili) e "." / ".." non si mostrano. */
        voci_viste++;
        if (le32(d) != 0 || strcmp(nome, ".") == 0 || strcmp(nome, "..") == 0) {
            free(nome);
            continue;
        }
        byte_nomi += nl;
        if (elenco->numero >= VOCI_MAX || byte_nomi > NOMI_BYTE_MAX) {
            free(nome);
            ph_errore(e, "cartella con troppe voci");
            esito = PH_FILE_ERRORE;
            break;
        }
        if (elenco->numero == cap) {
            size_t nuova = cap ? cap * 2 : 64;
            PhFileVoce *v = realloc(elenco->voci, nuova * sizeof *v);
            if (!v) {
                free(nome);
                ph_errore(e, "memoria esaurita");
                esito = PH_FILE_ERRORE;
                break;
            }
            elenco->voci = v;
            cap = nuova;
        }
        uint32_t modo = le32(d + 20);
        PhFileVoce *v = &elenco->voci[elenco->numero++];
        v->nome = nome;
        v->cartella = S_ISDIR((mode_t)modo);
        v->collegamento = S_ISLNK((mode_t)modo);
        v->dimensione = le64(d + 36);
        v->modificato = (int64_t)le64(d + 52);
    }
    /* adbd manda sempre "." e ".." di una cartella leggibile. Un LIS2 senza
     * nessuna voce vuol dire che l'apertura e' fallita (cartella protetta,
     * come /data): STA2 era riuscita, la lista no. */
    if (esito == PH_FILE_RIUSCITO && voci_viste == 0) {
        ph_errore(e, "%s: accesso non consentito dal sistema", cartella);
        esito = PH_FILE_NON_CONSENTITO;
    }
    if (esito == PH_FILE_RIUSCITO || esito == PH_FILE_NON_CONSENTITO)
        sync_quit(&s);
    ph_canale_chiudi(c);

    if (esito == PH_FILE_RIUSCITO)
        esito = risolvi_link(a, cartella, elenco, e);
    if (esito != PH_FILE_RIUSCITO) {
        ph_file_elenco_libera(elenco);
        return esito;
    }
    if (elenco->numero > 1)
        qsort(elenco->voci, elenco->numero, sizeof elenco->voci[0], confronta_voci);
    return PH_FILE_RIUSCITO;
}

PhFileEsito ph_file_info(PhAdb *a, const char *percorso, PhFileVoce *voce, PhErrore *e)
{
    if (!a || !percorso || !voce)
        return ph_errore(e, "info: argomenti mancanti"), PH_FILE_ERRORE;
    PhCanale *c = apri_sync(a, e);
    if (!c)
        return PH_FILE_ERRORE;
    Sync s = {c, NULL, e};
    Stat l = {0};
    PhFileEsito esito = sync_stat(&s, "LST2", percorso, &l);
    if (esito == PH_FILE_RIUSCITO) {
        voce->nome = NULL;
        voce->cartella = S_ISDIR((mode_t)l.modo);
        voce->collegamento = S_ISLNK((mode_t)l.modo);
        voce->dimensione = l.dimensione;
        voce->modificato = l.modificato;
        if (voce->collegamento) {
            /* Per un link interessa cio' a cui punta; se e' rotto resta il link. */
            Stat t = {0};
            PhFileEsito r = sync_stat(&s, "STA2", percorso, &t);
            if (r == PH_FILE_RIUSCITO) {
                voce->cartella = S_ISDIR((mode_t)t.modo);
                voce->dimensione = t.dimensione;
                voce->modificato = t.modificato;
            } else if (t.errore == 0) {
                esito = r;
            }
        }
    }
    if (esito == PH_FILE_RIUSCITO || esito == PH_FILE_NON_TROVATO || esito == PH_FILE_NON_CONSENTITO)
        sync_quit(&s);
    ph_canale_chiudi(c);
    return esito;
}
