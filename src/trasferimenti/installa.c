/* Installazione/disinstallazione di app: vedi installa.h. */
#include "trasferimenti/installa.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#include "comune/buffer.h"
#include "config/lingua.h"

#define PM_INSTALLA_MS 120000
#define PM_DISINSTALLA_MS 30000
#define PULIZIA_MS 5000
#define PREFISSO_TEMPORANEO "/data/local/tmp/yasde-apk-"
#define CD_MAX (32u * 1024 * 1024)
#define CODA_MAX (65535u + 22u)
#define PACCHETTO_MAX 255

/* ---- Riconoscimento dell'APK ------------------------------------------------ */

static uint16_t le16(const unsigned char *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t le32(const unsigned char *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* Legge esattamente n byte da off; false se il file e' piu' corto o errore. */
static bool leggi_esatto(int fd, void *buf, size_t n, off_t off)
{
    unsigned char *p = buf;
    while (n > 0) {
        ssize_t r = pread(fd, p, n, off);
        if (r < 0 && errno == EINTR)
            continue;
        if (r <= 0)
            return false;
        p += r;
        off += r;
        n -= (size_t)r;
    }
    return true;
}

static bool finisce_per_apk(const char *s)
{
    size_t n = strlen(s);
    return n > 4 && strcasecmp(s + n - 4, ".apk") == 0;
}

int ph_inst_riconosci(const char *percorso, PhErrore *e)
{
    if (!percorso)
        return ph_errore(e, "riconoscimento APK: percorso mancante");
    if (!finisce_per_apk(percorso))
        return 0;
    /* Prima stat (segue i collegamenti: un APK raggiunto da un link e' lecito):
     * una FIFO o un dispositivo chiamato x.apk non si apre nemmeno. Poi
     * O_NONBLOCK (se tra stat e open il nome cambiasse in una FIFO l'apertura
     * non blocca) e fstat dopo l'apertura. */
    struct stat pre;
    if (stat(percorso, &pre) != 0)
        return ph_errore(e, "%s: %s", percorso, strerror(errno));
    if (!S_ISREG(pre.st_mode))
        return 0;
    int fd = open(percorso, O_RDONLY | O_CLOEXEC | O_NONBLOCK);
    if (fd < 0)
        return ph_errore(e, "%s: %s", percorso, strerror(errno));
    int esito = 0;
    unsigned char *cd = NULL;
    struct stat st;
    if (fstat(fd, &st) != 0) {
        ph_errore(e, "%s: %s", percorso, strerror(errno));
        esito = -1;
        goto fine;
    }
    if (!S_ISREG(st.st_mode) || st.st_size < 22)
        goto fine;
    unsigned char testa[4];
    if (!leggi_esatto(fd, testa, 4, 0)) {
        ph_errore(e, "%s: lettura non riuscita", percorso);
        esito = -1;
        goto fine;
    }
    if (memcmp(testa, "PK\x03\x04", 4) != 0)
        goto fine;
    size_t dimensione = (size_t)st.st_size;
    size_t n_coda = dimensione < CODA_MAX ? dimensione : CODA_MAX;
    unsigned char *coda = malloc(n_coda);
    if (!coda) {
        ph_errore(e, "memoria esaurita");
        esito = -1;
        goto fine;
    }
    if (!leggi_esatto(fd, coda, n_coda, (off_t)(dimensione - n_coda))) {
        free(coda);
        ph_errore(e, "%s: lettura non riuscita", percorso);
        esito = -1;
        goto fine;
    }
    /* Fine della directory centrale: si cerca all'indietro la firma il cui
     * campo "lunghezza del commento" arriva esattamente alla fine del file
     * (un commento puo' contenere la stessa firma). */
    size_t eocd = (size_t)-1;
    for (size_t i = n_coda - 22 + 1; i-- > 0;) {
        if (le32(coda + i) == 0x06054b50u && i + 22 + le16(coda + i + 20) == n_coda) {
            eocd = i;
            break;
        }
    }
    uint32_t voci = 0, cd_dim = 0, cd_off = 0;
    if (eocd != (size_t)-1) {
        voci = le16(coda + eocd + 10);
        cd_dim = le32(coda + eocd + 12);
        cd_off = le32(coda + eocd + 16);
    }
    free(coda);
    if (eocd == (size_t)-1 || voci == 0xFFFF || cd_dim == 0xFFFFFFFFu || cd_off == 0xFFFFFFFFu)
        goto fine; /* non e' uno zip leggibile, o e' zip64 */
    if (cd_dim == 0 || cd_dim > CD_MAX || (uint64_t)cd_off + cd_dim > dimensione)
        goto fine;
    cd = malloc(cd_dim);
    if (!cd) {
        ph_errore(e, "memoria esaurita");
        esito = -1;
        goto fine;
    }
    if (!leggi_esatto(fd, cd, cd_dim, (off_t)cd_off)) {
        ph_errore(e, "%s: lettura non riuscita", percorso);
        esito = -1;
        goto fine;
    }
    size_t pos = 0;
    for (uint32_t i = 0; i < voci; i++) {
        if (cd_dim - pos < 46 || le32(cd + pos) != 0x02014b50u)
            break;
        size_t n = le16(cd + pos + 28), m = le16(cd + pos + 30), k = le16(cd + pos + 32);
        if (cd_dim - pos - 46 < n + m + k)
            break;
        if (n == 19 && memcmp(cd + pos + 46, "AndroidManifest.xml", 19) == 0) {
            esito = 1;
            break;
        }
        pos += 46 + n + m + k;
    }
fine:
    free(cd);
    close(fd);
    return esito;
}

/* ---- Nome del pacchetto ----------------------------------------------------- */

bool ph_inst_nome_pacchetto_valido(const char *nome)
{
    if (!nome || !nome[0])
        return false;
    size_t n = 0;
    for (const char *p = nome; *p; p++, n++) {
        bool lettera = (*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z');
        bool altro = (*p >= '0' && *p <= '9') || *p == '_' || *p == '.';
        if (n == 0 ? !lettera : !(lettera || altro))
            return false;
        if (n >= PACCHETTO_MAX)
            return false;
    }
    return true;
}

/* ---- Analisi dell'uscita di pm ---------------------------------------------- */

typedef struct {
    const char *codice;
    PhInstMotivo motivo;
    const char *testo; /* PH_T_: tradotto con ph_tv */
} Voce;

static const Voce voci_installa[] = {
    {"INSTALL_FAILED_ALREADY_EXISTS", PH_INST_M_GIA_INSTALLATA,
     PH_T_("Questa app e' gia' installata sul telefono e non si e' potuta aggiornare.")},
    {"INSTALL_FAILED_UPDATE_INCOMPATIBLE", PH_INST_M_FIRMA_DIVERSA,
     PH_T_("La versione gia' installata e' firmata da un altro autore: per metterla serve prima disinstallare "
           "quella che c'e' (i dati dell'app andrebbero persi).")},
    {"INSTALL_FAILED_VERSION_DOWNGRADE", PH_INST_M_VERSIONE_VECCHIA,
     PH_T_("Sul telefono c'e' gia' una versione piu' recente di questa app: non si torna a una piu' vecchia.")},
    {"INSTALL_FAILED_INSUFFICIENT_STORAGE", PH_INST_M_SPAZIO,
     PH_T_("Sul telefono non c'e' abbastanza spazio libero: libera un po' di memoria e riprova.")},
    {"INSTALL_FAILED_INVALID_APK", PH_INST_M_APK_NON_VALIDO,
     PH_T_("Il file non e' un'app valida: e' danneggiato o incompleto.")},
    {"INSTALL_FAILED_OLDER_SDK", PH_INST_M_ANDROID_VECCHIO,
     PH_T_("Questa app richiede una versione di Android piu' recente di quella del telefono.")},
    {"INSTALL_FAILED_NO_MATCHING_ABIS", PH_INST_M_ARCHITETTURA,
     PH_T_("Questa app non e' fatta per il processore di questo telefono.")},
    {"INSTALL_FAILED_USER_RESTRICTED", PH_INST_M_BLOCCATA,
     PH_T_("L'installazione e' stata bloccata dal telefono o da chi lo gestisce: controlla le impostazioni di "
           "sicurezza del telefono.")},
    {"INSTALL_FAILED_VERIFICATION_FAILURE", PH_INST_M_VERIFICA,
     PH_T_("Il controllo di sicurezza del telefono non ha approvato l'app.")},
    {"INSTALL_FAILED_VERIFICATION_TIMEOUT", PH_INST_M_VERIFICA,
     PH_T_("Il controllo di sicurezza del telefono non ha risposto in tempo.")},
    {"INSTALL_FAILED_TEST_ONLY", PH_INST_M_SOLO_PROVA,
     PH_T_("Questa e' una versione solo per prove degli sviluppatori e il telefono non la accetta.")},
    {"INSTALL_FAILED_DEPRECATED_SDK_VERSION", PH_INST_M_APP_TROPPO_VECCHIA,
     PH_T_("Questa app e' troppo vecchia per questa versione di Android.")},
    {"INSTALL_FAILED_MISSING_SPLIT", PH_INST_M_SPLIT_MANCANTE,
     PH_T_("Questa app e' divisa in piu' file: da un solo APK non si puo' installare.")},
    {"INSTALL_FAILED_ABORTED", PH_INST_M_ANNULLATA_SUL_TELEFONO,
     PH_T_("L'installazione e' stata annullata sul telefono.")},
};

static const Voce voci_disinstalla[] = {
    {"DELETE_FAILED_DEVICE_POLICY_MANAGER", PH_INST_M_BLOCCATA,
     PH_T_("Il telefono non permette di disinstallare questa app: e' protetta da chi gestisce il telefono.")},
    {"DELETE_FAILED_USER_RESTRICTED", PH_INST_M_BLOCCATA,
     PH_T_("Il telefono non permette di disinstallare questa app: la disinstallazione e' bloccata.")},
    {"DELETE_FAILED_OWNER_BLOCKED", PH_INST_M_BLOCCATA,
     PH_T_("Il telefono non permette di disinstallare questa app: la disinstallazione e' bloccata.")},
    {"DELETE_FAILED_INTERNAL_ERROR", PH_INST_M_SISTEMA,
     PH_T_("Il telefono non ha disinstallato l'app: di solito e' un'app di sistema, che non si puo' togliere, "
           "oppure non e' piu' installata.")},
    {"DELETE_FAILED_ABORTED", PH_INST_M_ANNULLATA_SUL_TELEFONO,
     PH_T_("La disinstallazione e' stata annullata sul telefono.")},
};

#define PREFISSO_APK_NON_VALIDO "INSTALL_PARSE_FAILED_"

static bool car_codice(unsigned char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
}

/* Ricerca di una sottostringa in un buffer senza '\0' finale. */
static const char *cerca(const char *b, size_t n, const char *ago)
{
    size_t k = strlen(ago);
    if (k == 0 || n < k)
        return NULL;
    for (size_t i = 0; i + k <= n; i++)
        if (b[i] == ago[0] && memcmp(b + i, ago, k) == 0)
            return b + i;
    return NULL;
}

static bool contiene_ci(const char *b, size_t n, const char *ago)
{
    size_t k = strlen(ago);
    for (size_t i = 0; i + k <= n; i++)
        if (strncasecmp(b + i, ago, k) == 0)
            return true;
    return false;
}

/* Copia in cod (capacita byte, terminata) il primo codice trovato nel buffer:
 * dopo "Failure [" oppure come parola INSTALL_/DELETE_. */
static bool trova_codice(const char *b, size_t n, char *cod, size_t cap)
{
    const char *f = cerca(b, n, "Failure [");
    size_t i;
    if (f) {
        i = (size_t)(f - b) + 9;
    } else {
        i = (size_t)-1;
        for (size_t j = 0; j < n; j++) {
            bool prefisso = (n - j >= 8 && memcmp(b + j, "INSTALL_", 8) == 0) ||
                            (n - j >= 7 && memcmp(b + j, "DELETE_", 7) == 0);
            if (prefisso && (j == 0 || !car_codice((unsigned char)b[j - 1]))) {
                i = j;
                break;
            }
        }
        if (i == (size_t)-1)
            return false;
    }
    size_t k = 0;
    while (i < n && k + 1 < cap && car_codice((unsigned char)b[i]))
        cod[k++] = b[i++];
    cod[k] = '\0';
    return k > 0;
}

/* "Success" come riga intera (spazi e \r ai lati ignorati). */
static bool ha_success(const char *b, size_t n)
{
    size_t i = 0;
    while (i < n) {
        size_t j = i;
        while (j < n && b[j] != '\n')
            j++;
        size_t a = i, z = j;
        while (a < z && (b[a] == ' ' || b[a] == '\t' || b[a] == '\r'))
            a++;
        while (z > a && (b[z - 1] == ' ' || b[z - 1] == '\t' || b[z - 1] == '\r'))
            z--;
        if (z - a == 7 && memcmp(b + a, "Success", 7) == 0)
            return true;
        i = j + 1;
    }
    return false;
}

/* Prima riga non vuota, ripulita (solo ASCII stampabile) e accorciata, in out. */
static void prima_riga(const char *b, size_t n, char *out, size_t cap)
{
    size_t i = 0, k = 0;
    while (i < n && (b[i] == '\n' || b[i] == '\r' || b[i] == ' ' || b[i] == '\t' || b[i] == '\0'))
        i++;
    while (i < n && b[i] != '\n' && b[i] != '\r' && k + 1 < cap && k < 120) {
        unsigned char c = (unsigned char)b[i++];
        out[k++] = c >= 0x20 && c < 0x7f ? (char)c : '?';
    }
    out[k] = '\0';
}

static void messaggio(PhInstRisultato *r, const char *modello_tradotto)
{
    snprintf(r->messaggio, sizeof r->messaggio, "%s", modello_tradotto);
}

void ph_inst_analizza(PhInstOperazione op, const char *uscita, size_t n_uscita, const char *errori,
                      size_t n_errori, PhInstRisultato *r)
{
    if (!r)
        return;
    if (!uscita)
        n_uscita = 0;
    if (!errori)
        n_errori = 0;
    r->esito = PH_INST_ERRORE;
    r->motivo = PH_INST_M_NESSUNO;
    r->codice[0] = '\0';
    bool disinstalla = op == PH_INST_OP_DISINSTALLA;

    /* pm uninstall di un pacchetto non installato per l'utente: "Failure [not
     * installed for 0]" (minuscolo, non e' un codice). */
    if (cerca(uscita, n_uscita, "not installed for") || cerca(errori, n_errori, "not installed for")) {
        r->esito = PH_INST_RIFIUTATO;
        r->motivo = PH_INST_M_NON_INSTALLATA;
        messaggio(r, ph_t("Questa app non risulta installata sul telefono."));
        return;
    }
    char cod[sizeof r->codice] = "";
    bool trovato = trova_codice(uscita, n_uscita, cod, sizeof cod) || trova_codice(errori, n_errori, cod, sizeof cod);
    if (!trovato && (ha_success(uscita, n_uscita) || ha_success(errori, n_errori))) {
        r->esito = PH_INST_RIUSCITO;
        messaggio(r, disinstalla ? ph_t("App disinstallata.") : ph_t("App installata sul telefono."));
        return;
    }
    if (trovato) {
        snprintf(r->codice, sizeof r->codice, "%s", cod);
        r->esito = PH_INST_RIFIUTATO;
        const Voce *tab = disinstalla ? voci_disinstalla : voci_installa;
        size_t nt = disinstalla ? sizeof voci_disinstalla / sizeof *voci_disinstalla
                                : sizeof voci_installa / sizeof *voci_installa;
        for (size_t i = 0; i < nt; i++) {
            if (strcmp(tab[i].codice, cod) == 0) {
                r->motivo = tab[i].motivo;
                messaggio(r, ph_tv(tab[i].testo));
                return;
            }
        }
        if (!disinstalla && strncmp(cod, PREFISSO_APK_NON_VALIDO, sizeof PREFISSO_APK_NON_VALIDO - 1) == 0) {
            r->motivo = PH_INST_M_APK_NON_VALIDO;
            messaggio(r, ph_t("Il file non e' un'app valida: e' danneggiato, incompleto o non e' firmato."));
            return;
        }
        r->motivo = PH_INST_M_ALTRO;
        ph_lingua_componi(r->messaggio, sizeof r->messaggio,
                          disinstalla ? ph_t("Il telefono non ha disinstallato l'app (codice: {codice}).")
                                      : ph_t("Il telefono ha rifiutato l'installazione (codice: {codice})."),
                          "codice", cod, (const char *)NULL);
        return;
    }
    /* I due rami seguenti (testi "Unknown package" e "system app") NON sono
     * verificati su un telefono vero: probabilmente non scattano mai; sono
     * innocui perche' valgono solo per la disinstallazione e senza codice. */
    if (disinstalla && (cerca(uscita, n_uscita, "Unknown package") || cerca(errori, n_errori, "Unknown package"))) {
        r->esito = PH_INST_RIFIUTATO;
        r->motivo = PH_INST_M_NON_INSTALLATA;
        messaggio(r, ph_t("Questa app non risulta installata sul telefono."));
        return;
    }
    if (disinstalla && (contiene_ci(uscita, n_uscita, "system app") || contiene_ci(errori, n_errori, "system app"))) {
        r->esito = PH_INST_RIFIUTATO;
        r->motivo = PH_INST_M_SISTEMA;
        messaggio(r, ph_t("Questa e' un'app di sistema: non si puo' disinstallare."));
        return;
    }
    /* Niente di riconoscibile: mai considerarlo riuscito. */
    char riga[128];
    prima_riga(uscita, n_uscita, riga, sizeof riga);
    if (riga[0] == '\0')
        prima_riga(errori, n_errori, riga, sizeof riga);
    if (riga[0] == '\0') {
        messaggio(r, ph_t("Il telefono non ha dato una risposta comprensibile."));
        return;
    }
    snprintf(r->codice, sizeof r->codice, "%.90s", riga);
    ph_lingua_componi(r->messaggio, sizeof r->messaggio,
                      ph_t("Il telefono ha risposto in modo inatteso: {codice}"), "codice", r->codice,
                      (const char *)NULL);
}

/* ---- Comandi sul telefono ---------------------------------------------------- */

static void azzera(PhInstRisultato *r)
{
    memset(r, 0, sizeof *r);
    r->esito = PH_INST_ERRORE;
}

static int nome_temporaneo(char *out, size_t n, PhErrore *e)
{
    uint32_t v = 0;
    size_t letti = 0;
    FILE *f = fopen("/dev/urandom", "rb");
    if (f) {
        letti = fread(&v, 1, sizeof v, f);
        fclose(f);
    }
    if (letti != sizeof v)
        return ph_errore(e, "impossibile ottenere un nome casuale per il file temporaneo");
    int k = snprintf(out, n, PREFISSO_TEMPORANEO "%08x.apk", (unsigned)v);
    if (k < 0 || (size_t)k >= n)
        return ph_errore(e, "nome del file temporaneo troppo lungo");
    return 0;
}

/* Esegue un comando e, se riuscito, lascia l'uscita nei buffer (da liberare). */
static int esegui(PhAdb *a, const char *cmd, PhBuffer *out, PhBuffer *err, int timeout_ms, PhErrore *e)
{
    int codice = 0;
    return ph_shell_esegui(a, cmd, out, err, &codice, timeout_ms, e);
}

PhInstEsito ph_inst_installa(PhAdb *a, const char *apk, PhInstProgresso progresso, void *utente,
                             const atomic_bool *annulla, PhInstRisultato *r, PhErrore *e)
{
    PhInstRisultato locale;
    if (!r)
        r = &locale;
    azzera(r);
    if (!a || !apk) {
        ph_errore(e, "installazione: argomenti mancanti");
        messaggio(r, ph_t("Installazione non possibile."));
        return r->esito;
    }
    int ric = ph_inst_riconosci(apk, e);
    if (ric != 1) {
        if (ric < 0) {
            messaggio(r, ph_t("Non riesco a leggere il file."));
        } else {
            r->motivo = PH_INST_M_APK_NON_VALIDO;
            messaggio(r, ph_t("Questo file non e' un'app Android (APK): non lo installo."));
            ph_errore(e, "%s non e' un APK", apk);
        }
        return r->esito;
    }
    char tmp[sizeof r->temporaneo];
    if (nome_temporaneo(tmp, sizeof tmp, e) != 0) {
        messaggio(r, ph_t("Installazione non possibile."));
        return r->esito;
    }
    PhFileEsito fe = ph_file_invia(a, apk, tmp, progresso, utente, annulla, e);
    if (fe == PH_FILE_ANNULLATO) {
        r->esito = PH_INST_ANNULLATO;
        messaggio(r, ph_t("Installazione annullata."));
    } else if (fe != PH_FILE_RIUSCITO) {
        r->esito = PH_INST_ERRORE;
        /* il dettaglio tecnico (in italiano, con percorsi) resta solo in *e */
        messaggio(r, ph_t("Non sono riuscito a copiare l'app sul telefono."));
    } else if (annulla && atomic_load(annulla)) {
        r->esito = PH_INST_ANNULLATO;
        messaggio(r, ph_t("Installazione annullata."));
    } else {
        /* tmp contiene solo [a-z0-9/.-]: gli apici sono una cintura in piu' */
        char cmd[sizeof tmp + 32];
        snprintf(cmd, sizeof cmd, "pm install -r '%s'", tmp);
        PhBuffer out = PH_BUFFER_VUOTO, err = PH_BUFFER_VUOTO;
        PhErrore e2 = {{0}};
        int rc = esegui(a, cmd, &out, &err, PM_INSTALLA_MS, &e2);
        if (rc != 0) {
            r->esito = PH_INST_ERRORE;
            messaggio(r, ph_t("Il telefono non ha risposto all'installazione: non so se l'app sia stata "
                              "installata."));
            if (e && !e->testo[0])
                snprintf(e->testo, sizeof e->testo, "%s", e2.testo);
        } else {
            ph_inst_analizza(PH_INST_OP_INSTALLA, (const char *)out.dati, out.lunghezza, (const char *)err.dati,
                             err.lunghezza, r);
        }
        ph_buffer_libera(&out);
        ph_buffer_libera(&err);
    }

    /* Pulizia: SEMPRE, anche su errore, annullamento o connessione caduta. */
    char rm[sizeof tmp + 16];
    snprintf(rm, sizeof rm, "rm -f '%s'", tmp);
    PhErrore e3 = {{0}};
    int codice = -1;
    int rc = ph_shell_esegui(a, rm, NULL, NULL, &codice, PULIZIA_MS, &e3);
    if (rc != 0 || codice != 0) {
        r->temporaneo_rimasto = true;
        snprintf(r->temporaneo, sizeof r->temporaneo, "%s", tmp);
        size_t l = strlen(r->messaggio);
        snprintf(r->messaggio + l, sizeof r->messaggio - l, "%s",
                 ph_t(" Il file temporaneo sul telefono verra' tolto alla prossima connessione."));
    }
    return r->esito;
}

int ph_inst_pulisci(PhAdb *a, PhErrore *e)
{
    if (!a)
        return ph_errore(e, "pulizia: connessione mancante");
    int codice = -1;
    PhErrore e2 = {{0}};
    /* Riga fissa. Il jar del componente (yasde-<8 cifre>.jar) e i file
     * yasde-custode-* non corrispondono: prefisso diverso e fine ".apk". */
    if (ph_shell_esegui(a, "rm -f /data/local/tmp/yasde-apk-*.apk", NULL, NULL, &codice, PULIZIA_MS, &e2) != 0)
        return ph_errore(e, "pulizia dei file temporanei: %s", e2.testo);
    if (codice != 0)
        return ph_errore(e, "pulizia dei file temporanei: codice di uscita %d", codice);
    return 0;
}

PhInstEsito ph_inst_disinstalla(PhAdb *a, const char *pacchetto, PhInstRisultato *r, PhErrore *e)
{
    PhInstRisultato locale;
    if (!r)
        r = &locale;
    azzera(r);
    if (!a || !ph_inst_nome_pacchetto_valido(pacchetto)) {
        ph_errore(e, "disinstallazione: nome di pacchetto non valido");
        messaggio(r, ph_t("Il nome dell'app non e' valido: non la disinstallo."));
        return r->esito;
    }
    char cmd[PACCHETTO_MAX + 32];
    snprintf(cmd, sizeof cmd, "pm uninstall '%s'", pacchetto);
    PhBuffer out = PH_BUFFER_VUOTO, err = PH_BUFFER_VUOTO;
    PhErrore e2 = {{0}};
    if (esegui(a, cmd, &out, &err, PM_DISINSTALLA_MS, &e2) != 0) {
        r->esito = PH_INST_ERRORE;
        messaggio(r, ph_t("Il telefono non ha risposto alla disinstallazione: non so se l'app sia stata "
                          "tolta."));
        if (e)
            snprintf(e->testo, sizeof e->testo, "%s", e2.testo);
    } else {
        ph_inst_analizza(PH_INST_OP_DISINSTALLA, (const char *)out.dati, out.lunghezza, (const char *)err.dati,
                         err.lunghezza, r);
    }
    ph_buffer_libera(&out);
    ph_buffer_libera(&err);
    return r->esito;
}
