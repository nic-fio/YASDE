/* Prove di installa.c con il telefono finto (finto_installa.c). */
#include <errno.h>
#include <fcntl.h>
#include <ftw.h>
#include <limits.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "finto_installa.h"
#include "trasferimenti/installa.h"

static int guasti;
#define CONTROLLA(c)                                                                  \
    do {                                                                              \
        if (!(c)) {                                                                   \
            fprintf(stderr, "%s:%d: fallito: %s\n", __FILE__, __LINE__, #c);          \
            guasti++;                                                                 \
        }                                                                             \
    } while (0)

static char base[400];

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
    snprintf(base, sizeof base, "%s/ph_inst_XXXXXX", t && *t ? t : "/tmp");
    if (!mkdtemp(base)) {
        perror("mkdtemp");
        exit(2);
    }
    finto_inst_reset();
}

static const char *pp(const char *nome)
{
    static __thread char b[4][PATH_MAX];
    static __thread int k;
    k = (k + 1) % 4;
    snprintf(b[k], PATH_MAX, "%s/%s", base, nome);
    return b[k];
}

/* ---- Costruzione di zip a mano ---------------------------------------------- */

static void p16(FILE *f, unsigned v) { fputc((int)(v & 255), f); fputc((int)((v >> 8) & 255), f); }
static void p32(FILE *f, unsigned long v) { p16(f, (unsigned)(v & 0xffff)); p16(f, (unsigned)((v >> 16) & 0xffff)); }

/* Zip con voci vuote (solo i nomi). `lun_falsa`: se > 0 la lunghezza del nome
 * dell'ultima voce della directory centrale e' gonfiata a quel valore. */
static void crea_zip(const char *percorso, const char *const *nomi, int n, const void *commento, size_t lc,
                     unsigned lun_falsa, unsigned long off_falso)
{
    FILE *f = fopen(percorso, "wb");
    if (!f) {
        perror(percorso);
        exit(2);
    }
    unsigned long off[16];
    for (int i = 0; i < n; i++) {
        off[i] = (unsigned long)ftell(f);
        fwrite("PK\x03\x04", 1, 4, f);
        p16(f, 20); p16(f, 0); p16(f, 0); p16(f, 0); p16(f, 0);
        p32(f, 0); p32(f, 0); p32(f, 0);
        p16(f, (unsigned)strlen(nomi[i])); p16(f, 0);
        fwrite(nomi[i], 1, strlen(nomi[i]), f);
    }
    unsigned long cd = (unsigned long)ftell(f);
    for (int i = 0; i < n; i++) {
        unsigned ln = (unsigned)strlen(nomi[i]);
        fwrite("PK\x01\x02", 1, 4, f);
        p16(f, 20); p16(f, 20); p16(f, 0); p16(f, 0); p16(f, 0); p16(f, 0);
        p32(f, 0); p32(f, 0); p32(f, 0);
        p16(f, i == n - 1 && lun_falsa ? lun_falsa : ln); p16(f, 0); p16(f, 0);
        p16(f, 0); p16(f, 0); p32(f, 0); p32(f, off[i]);
        fwrite(nomi[i], 1, ln, f);
    }
    unsigned long fine = (unsigned long)ftell(f);
    fwrite("PK\x05\x06", 1, 4, f);
    p16(f, 0); p16(f, 0); p16(f, (unsigned)n); p16(f, (unsigned)n);
    p32(f, fine - cd);
    p32(f, off_falso ? off_falso : cd);
    p16(f, (unsigned)lc);
    if (lc)
        fwrite(commento, 1, lc, f);
    fclose(f);
}

static void scrivi_testo(const char *percorso, const void *d, size_t n)
{
    FILE *f = fopen(percorso, "wb");
    if (!f)
        exit(2);
    if (n)
        fwrite(d, 1, n, f);
    fclose(f);
}

static void prova_riconosci(void)
{
    ambiente_nuovo();
    PhErrore e = {{0}};
    const char *solo[] = {"AndroidManifest.xml"};
    const char *molti[] = {"classes.dex", "res/a.png", "META-INF/CERT.RSA", "AndroidManifest.xml", "z"};
    const char *senza[] = {"classes.dex", "res/AndroidManifest.xml", "AndroidManifest.xmlx", "xAndroidManifest.xml"};
    crea_zip(pp("ok.apk"), solo, 1, NULL, 0, 0, 0);
    crea_zip(pp("OK2.APK"), molti, 5, NULL, 0, 0, 0);
    crea_zip(pp("senza.apk"), senza, 4, NULL, 0, 0, 0);
    crea_zip(pp("valido.zip"), solo, 1, NULL, 0, 0, 0);       /* contenuto giusto, nome sbagliato */
    crea_zip(pp("cdfalso.apk"), solo, 1, NULL, 0, 0, 100000); /* directory centrale fuori dal file */
    crea_zip(pp("nomelungo.apk"), solo, 1, NULL, 0, 60000, 0);  /* lunghezza del nome oltre la directory */
    /* commento con dentro una falsa firma di fine directory */
    static const unsigned char falso[] = {'x', 'P', 'K', 5, 6, 0, 0, 0, 0, 1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    crea_zip(pp("commento.apk"), solo, 1, falso, sizeof falso, 0, 0);
    scrivi_testo(pp("testo.apk"), "questo non e' uno zip, per niente\n", 35);
    scrivi_testo(pp("vuoto.apk"), "", 0);
    scrivi_testo(pp("pochi.apk"), "PK\x03\x04", 4);
    scrivi_testo(pp("solofirma.apk"), "PK\x03\x04" "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", 68);
    mkdir(pp("cartella.apk"), 0755);
    CONTROLLA(ph_inst_riconosci(pp("ok.apk"), &e) == 1);
    CONTROLLA(ph_inst_riconosci(pp("OK2.APK"), &e) == 1);
    CONTROLLA(ph_inst_riconosci(pp("senza.apk"), &e) == 0);
    CONTROLLA(ph_inst_riconosci(pp("valido.zip"), &e) == 0);
    CONTROLLA(ph_inst_riconosci(pp("cdfalso.apk"), &e) == 0);
    CONTROLLA(ph_inst_riconosci(pp("nomelungo.apk"), &e) == 0);
    CONTROLLA(ph_inst_riconosci(pp("commento.apk"), &e) == 1);
    CONTROLLA(ph_inst_riconosci(pp("testo.apk"), &e) == 0);
    CONTROLLA(ph_inst_riconosci(pp("vuoto.apk"), &e) == 0);
    CONTROLLA(ph_inst_riconosci(pp("pochi.apk"), &e) == 0);
    CONTROLLA(ph_inst_riconosci(pp("solofirma.apk"), &e) == 0);
    CONTROLLA(ph_inst_riconosci(pp("cartella.apk"), &e) == 0);
    /* una FIFO chiamata x.apk non blocca (nessuno scrive dall'altra parte) */
    CONTROLLA(mkfifo(pp("fifo.apk"), 0600) == 0);
    CONTROLLA(ph_inst_riconosci(pp("fifo.apk"), &e) == 0);
    /* un collegamento a un APK vero e' lecito, uno a una FIFO no */
    CONTROLLA(symlink(pp("ok.apk"), pp("link.apk")) == 0);
    CONTROLLA(ph_inst_riconosci(pp("link.apk"), &e) == 1);
    CONTROLLA(symlink(pp("fifo.apk"), pp("linkfifo.apk")) == 0);
    CONTROLLA(ph_inst_riconosci(pp("linkfifo.apk"), &e) == 0);
    CONTROLLA(symlink(pp("non-esiste-niente"), pp("pendente.apk")) == 0);
    CONTROLLA(ph_inst_riconosci(pp("pendente.apk"), &e) == -1);
    e.testo[0] = 0;
    e.testo[0] = 0;
    CONTROLLA(ph_inst_riconosci(pp("non-esiste.apk"), &e) == -1 && e.testo[0]);
    CONTROLLA(ph_inst_riconosci(NULL, NULL) == -1);
    /* troncato: tolti gli ultimi byte (la fine della directory sparisce) */
    {
        FILE *f = fopen(pp("ok.apk"), "rb");
        unsigned char b[400];
        size_t n = fread(b, 1, sizeof b, f);
        fclose(f);
        for (size_t k = 1; k < 30 && k < n; k++) {
            scrivi_testo(pp("tronco.apk"), b, n - k);
            CONTROLLA(ph_inst_riconosci(pp("tronco.apk"), NULL) == 0);
        }
    }
    /* byte rovinati a caso: mai un errore di memoria (ASan), solo 0 o 1 */
    {
        FILE *f = fopen(pp("molti.apk"), "rb");
        (void)f;
        crea_zip(pp("molti.apk"), molti, 5, NULL, 0, 0, 0);
        f = fopen(pp("molti.apk"), "rb");
        unsigned char b[1000];
        size_t n = fread(b, 1, sizeof b, f);
        fclose(f);
        unsigned seme = 7;
        for (int giro = 0; giro < 2000; giro++) {
            unsigned char c[1000];
            memcpy(c, b, n);
            for (int k = 0; k < 3; k++) {
                seme = seme * 1103515245u + 12345u;
                c[(seme >> 8) % n] = (unsigned char)(seme >> 20);
            }
            scrivi_testo(pp("rotto.apk"), c, n);
            int r = ph_inst_riconosci(pp("rotto.apk"), NULL);
            CONTROLLA(r == 0 || r == 1);
        }
    }
}

/* ---- Analisi dell'uscita di pm ------------------------------------------------- */

typedef struct {
    PhInstOperazione op;
    const char *out, *err;
    PhInstEsito esito;
    PhInstMotivo motivo;
    const char *codice;
} CasoPm;

static void prova_analisi(void)
{
    static const CasoPm casi[] = {
        {PH_INST_OP_INSTALLA, "Performing Streamed Install\nSuccess\n", "", PH_INST_RIUSCITO, PH_INST_M_NESSUNO, ""},
        {PH_INST_OP_INSTALLA, "Success\r\n", "", PH_INST_RIUSCITO, PH_INST_M_NESSUNO, ""},
        {PH_INST_OP_INSTALLA, "  Success  ", "", PH_INST_RIUSCITO, PH_INST_M_NESSUNO, ""},
        {PH_INST_OP_INSTALLA, "Failure [INSTALL_FAILED_ALREADY_EXISTS: Attempt to re-install com.example.x without first uninstalling.]\n", "",
         PH_INST_RIFIUTATO, PH_INST_M_GIA_INSTALLATA, "INSTALL_FAILED_ALREADY_EXISTS"},
        {PH_INST_OP_INSTALLA, "Failure [INSTALL_FAILED_UPDATE_INCOMPATIBLE: Package com.example.x signatures do not match previously installed version; ignoring!]\n", "",
         PH_INST_RIFIUTATO, PH_INST_M_FIRMA_DIVERSA, "INSTALL_FAILED_UPDATE_INCOMPATIBLE"},
        {PH_INST_OP_INSTALLA, "Failure [INSTALL_FAILED_VERSION_DOWNGRADE: Downgrade detected: Update version code 1 is older than current 2]\n", "",
         PH_INST_RIFIUTATO, PH_INST_M_VERSIONE_VECCHIA, "INSTALL_FAILED_VERSION_DOWNGRADE"},
        {PH_INST_OP_INSTALLA, "Failure [INSTALL_FAILED_INSUFFICIENT_STORAGE]\n", "", PH_INST_RIFIUTATO, PH_INST_M_SPAZIO,
         "INSTALL_FAILED_INSUFFICIENT_STORAGE"},
        {PH_INST_OP_INSTALLA, "Failure [INSTALL_FAILED_INVALID_APK: Failed to parse /data/local/tmp/x.apk]\n", "",
         PH_INST_RIFIUTATO, PH_INST_M_APK_NON_VALIDO, "INSTALL_FAILED_INVALID_APK"},
        {PH_INST_OP_INSTALLA, "Failure [INSTALL_PARSE_FAILED_NOT_APK: Failed to parse /data/local/tmp/x.apk: Missing AndroidManifest.xml]\n", "",
         PH_INST_RIFIUTATO, PH_INST_M_APK_NON_VALIDO, "INSTALL_PARSE_FAILED_NOT_APK"},
        {PH_INST_OP_INSTALLA, "Failure [INSTALL_PARSE_FAILED_NO_CERTIFICATES: Failed to collect certificates]\n", "",
         PH_INST_RIFIUTATO, PH_INST_M_APK_NON_VALIDO, "INSTALL_PARSE_FAILED_NO_CERTIFICATES"},
        {PH_INST_OP_INSTALLA, "Failure [INSTALL_FAILED_OLDER_SDK: Requires newer sdk version #34 (current version is #30)]\n", "",
         PH_INST_RIFIUTATO, PH_INST_M_ANDROID_VECCHIO, "INSTALL_FAILED_OLDER_SDK"},
        {PH_INST_OP_INSTALLA, "Failure [INSTALL_FAILED_NO_MATCHING_ABIS: INSTALL_FAILED_NO_MATCHING_ABIS: Failed to extract native libraries, res=-113]\n", "",
         PH_INST_RIFIUTATO, PH_INST_M_ARCHITETTURA, "INSTALL_FAILED_NO_MATCHING_ABIS"},
        {PH_INST_OP_INSTALLA, "Failure [INSTALL_FAILED_USER_RESTRICTED: Install canceled by user]\n", "", PH_INST_RIFIUTATO,
         PH_INST_M_BLOCCATA, "INSTALL_FAILED_USER_RESTRICTED"},
        {PH_INST_OP_INSTALLA, "Failure [INSTALL_FAILED_VERIFICATION_FAILURE: Package verification failed]\n", "", PH_INST_RIFIUTATO,
         PH_INST_M_VERIFICA, "INSTALL_FAILED_VERIFICATION_FAILURE"},
        {PH_INST_OP_INSTALLA, "Failure [INSTALL_FAILED_TEST_ONLY: installPackageLI]\n", "", PH_INST_RIFIUTATO,
         PH_INST_M_SOLO_PROVA, "INSTALL_FAILED_TEST_ONLY"},
        {PH_INST_OP_INSTALLA, "Failure [INSTALL_FAILED_ABORTED: User rejected permissions]\n", "", PH_INST_RIFIUTATO,
         PH_INST_M_ANNULLATA_SUL_TELEFONO, "INSTALL_FAILED_ABORTED"},
        {PH_INST_OP_INSTALLA, "Failure [INSTALL_FAILED_STRANO_E_NUOVO: boh]\n", "", PH_INST_RIFIUTATO, PH_INST_M_ALTRO,
         "INSTALL_FAILED_STRANO_E_NUOVO"},
        /* solo su stderr, con il prefisso di adb */
        {PH_INST_OP_INSTALLA, "", "adb: failed to install x.apk: Failure [INSTALL_FAILED_ALREADY_EXISTS: x]\n",
         PH_INST_RIFIUTATO, PH_INST_M_GIA_INSTALLATA, "INSTALL_FAILED_ALREADY_EXISTS"},
        /* codice senza "Failure [" */
        {PH_INST_OP_INSTALLA, "Error: INSTALL_FAILED_OLDER_SDK\n", "", PH_INST_RIFIUTATO, PH_INST_M_ANDROID_VECCHIO,
         "INSTALL_FAILED_OLDER_SDK"},
        /* nessuna riga "Success" intera */
        {PH_INST_OP_INSTALLA, "Not Success\n", "", PH_INST_ERRORE, PH_INST_M_NESSUNO, "Not Success"},
        {PH_INST_OP_INSTALLA, "Exception occurred while executing 'install':\njava.lang.SecurityException: Shell does not have permission\n", "",
         PH_INST_ERRORE, PH_INST_M_NESSUNO, "Exception occurred while executing 'install':"},
        {PH_INST_OP_INSTALLA, "", "", PH_INST_ERRORE, PH_INST_M_NESSUNO, ""},
        {PH_INST_OP_INSTALLA, "\n\n  \n", "", PH_INST_ERRORE, PH_INST_M_NESSUNO, ""},
        {PH_INST_OP_INSTALLA, "Failure [INSTALL_FAILED_DEPRECATED_SDK_VERSION: App targets SDK 20]\n", "", PH_INST_RIFIUTATO,
         PH_INST_M_APP_TROPPO_VECCHIA, "INSTALL_FAILED_DEPRECATED_SDK_VERSION"},
        {PH_INST_OP_INSTALLA, "Failure [INSTALL_FAILED_MISSING_SPLIT: Missing split for com.x]\n", "", PH_INST_RIFIUTATO,
         PH_INST_M_SPLIT_MANCANTE, "INSTALL_FAILED_MISSING_SPLIT"},
        {PH_INST_OP_DISINSTALLA, "Failure [not installed for 0]\n", "", PH_INST_RIFIUTATO, PH_INST_M_NON_INSTALLATA, ""},
        /* disinstallazione */
        {PH_INST_OP_DISINSTALLA, "Success\n", "", PH_INST_RIUSCITO, PH_INST_M_NESSUNO, ""},
        {PH_INST_OP_DISINSTALLA, "Failure [DELETE_FAILED_DEVICE_POLICY_MANAGER]\n", "", PH_INST_RIFIUTATO, PH_INST_M_BLOCCATA,
         "DELETE_FAILED_DEVICE_POLICY_MANAGER"},
        {PH_INST_OP_DISINSTALLA, "Failure [DELETE_FAILED_INTERNAL_ERROR]\n", "", PH_INST_RIFIUTATO, PH_INST_M_SISTEMA,
         "DELETE_FAILED_INTERNAL_ERROR"},
        {PH_INST_OP_DISINSTALLA, "Failure [DELETE_FAILED_USER_RESTRICTED]\n", "", PH_INST_RIFIUTATO, PH_INST_M_BLOCCATA,
         "DELETE_FAILED_USER_RESTRICTED"},
        {PH_INST_OP_DISINSTALLA, "Failure [DELETE_FAILED_OWNER_BLOCKED]\n", "", PH_INST_RIFIUTATO, PH_INST_M_BLOCCATA,
         "DELETE_FAILED_OWNER_BLOCKED"},
        {PH_INST_OP_DISINSTALLA, "Failure [DELETE_FAILED_ABORTED]\n", "", PH_INST_RIFIUTATO, PH_INST_M_ANNULLATA_SUL_TELEFONO,
         "DELETE_FAILED_ABORTED"},
        {PH_INST_OP_DISINSTALLA, "Failure [DELETE_FAILED_NUOVO]\n", "", PH_INST_RIFIUTATO, PH_INST_M_ALTRO,
         "DELETE_FAILED_NUOVO"},
        {PH_INST_OP_DISINSTALLA, "", "java.lang.IllegalArgumentException: Unknown package: com.nessuna\n", PH_INST_RIFIUTATO,
         PH_INST_M_NON_INSTALLATA, ""},
        {PH_INST_OP_DISINSTALLA, "Error: Cannot uninstall System app com.android.x\n", "", PH_INST_RIFIUTATO, PH_INST_M_SISTEMA, ""},
        /* i codici di installazione non valgono come tabella di disinstallazione e viceversa */
        {PH_INST_OP_DISINSTALLA, "Failure [INSTALL_FAILED_ALREADY_EXISTS]\n", "", PH_INST_RIFIUTATO, PH_INST_M_ALTRO,
         "INSTALL_FAILED_ALREADY_EXISTS"},
        {PH_INST_OP_INSTALLA, "Failure [DELETE_FAILED_INTERNAL_ERROR]\n", "", PH_INST_RIFIUTATO, PH_INST_M_ALTRO,
         "DELETE_FAILED_INTERNAL_ERROR"},
    };
    for (size_t i = 0; i < sizeof casi / sizeof *casi; i++) {
        const CasoPm *c = &casi[i];
        /* buffer di lunghezza esatta, senza '\0' finale: ASan vede ogni lettura fuori misura */
        size_t no = strlen(c->out), ne = strlen(c->err);
        char *bo = malloc(no ? no : 1), *be = malloc(ne ? ne : 1);
        memcpy(bo, c->out, no);
        memcpy(be, c->err, ne);
        PhInstRisultato r;
        memset(&r, 0x55, sizeof r);
        r.temporaneo_rimasto = false;
        ph_inst_analizza(c->op, bo, no, be, ne, &r);
        if (r.esito != c->esito || r.motivo != c->motivo || strcmp(r.codice, c->codice) != 0)
            fprintf(stderr, "caso %zu: esito %d motivo %d codice '%s'\n", i, r.esito, r.motivo, r.codice);
        CONTROLLA(r.esito == c->esito);
        CONTROLLA(r.motivo == c->motivo);
        CONTROLLA(strcmp(r.codice, c->codice) == 0);
        CONTROLLA(r.messaggio[0] != '\0' && strlen(r.messaggio) < sizeof r.messaggio);
        if (c->motivo == PH_INST_M_ALTRO)
            CONTROLLA(strstr(r.messaggio, c->codice) != NULL); /* il testo generico riporta il codice */
        free(bo);
        free(be);
    }
    /* NULL con lunghezza zero, risultato NULL */
    PhInstRisultato r;
    ph_inst_analizza(PH_INST_OP_INSTALLA, NULL, 0, NULL, 0, &r);
    CONTROLLA(r.esito == PH_INST_ERRORE);
    ph_inst_analizza(PH_INST_OP_INSTALLA, "x", 1, "y", 1, NULL);

    /* codice lunghissimo, senza chiusura, a fine buffer, con byte nulli e binari */
    size_t N = 5000;
    char *g = malloc(N);
    memset(g, 'A', N);
    memcpy(g, "Failure [INSTALL_", 17);
    ph_inst_analizza(PH_INST_OP_INSTALLA, g, N, NULL, 0, &r);
    CONTROLLA(r.esito == PH_INST_RIFIUTATO && strlen(r.codice) < sizeof r.codice && strlen(r.messaggio) < sizeof r.messaggio);
    ph_inst_analizza(PH_INST_OP_INSTALLA, g, 9, NULL, 0, &r); /* finisce dopo "Failure [" */
    CONTROLLA(r.esito == PH_INST_ERRORE);
    ph_inst_analizza(PH_INST_OP_INSTALLA, g, 12, NULL, 0, &r);
    CONTROLLA(strlen(r.messaggio) < sizeof r.messaggio);
    free(g);
    unsigned seme = 99;
    for (int giro = 0; giro < 20000; giro++) {
        size_t n = (seme = seme * 1103515245u + 12345u) % 200;
        unsigned char *b = malloc(n ? n : 1);
        for (size_t k = 0; k < n; k++)
            b[k] = (unsigned char)((seme = seme * 1103515245u + 12345u) >> 16);
        if (giro % 3 == 0 && n > 30)
            memcpy(b + n / 3, "Failure [INSTALL_X", 18);
        if (giro % 5 == 0 && n > 12)
            memcpy(b + 2, "\nSuccess\n", 9);
        ph_inst_analizza((PhInstOperazione)(giro & 1), (char *)b, n, (char *)b, n, &r);
        CONTROLLA(strlen(r.messaggio) < sizeof r.messaggio && strlen(r.codice) < sizeof r.codice);
        CONTROLLA(r.esito == PH_INST_RIUSCITO || r.esito == PH_INST_RIFIUTATO || r.esito == PH_INST_ERRORE);
        free(b);
    }
}

/* ---- Installazione e disinstallazione con il telefono finto -------------------- */

typedef struct {
    atomic_bool *annulla;
    int ultimo_blocco;
    uint64_t prec;
    int chiamate;
    int monotono;
    int annulla_a;          /* alla chiamata n imposta *annulla (0 = mai) */
} Pr;

static void progresso(uint64_t fatti, uint64_t totale, void *u)
{
    Pr *p = u;
    p->chiamate++;
    if (fatti < p->prec || totale != 300)
        p->monotono = 0;
    p->prec = fatti;
    if (p->annulla_a && p->chiamate == p->annulla_a)
        atomic_store(p->annulla, true);
}

static void prepara_apk(void)
{
    const char *solo[] = {"AndroidManifest.xml"};
    crea_zip(pp("app.apk"), solo, 1, NULL, 0, 0, 0);
}

/* Il temporaneo ha il nome previsto e la pulizia e' stata fatta. */
static void controlla_pulizia(const char *tmp, size_t primo_cmd_rm)
{
    char *rm = finto_inst_comando(primo_cmd_rm);
    char atteso[200];
    snprintf(atteso, sizeof atteso, "rm -f '%s'", tmp);
    CONTROLLA(rm && strcmp(rm, atteso) == 0);
    free(rm);
}

static bool nome_nostro(const char *p)
{
    const char *pre = "/data/local/tmp/yasde-apk-";
    if (strncmp(p, pre, strlen(pre)) != 0 || strlen(p) != strlen(pre) + 8 + 4 || strcmp(p + strlen(p) - 4, ".apk"))
        return false;
    for (size_t i = strlen(pre); i < strlen(pre) + 8; i++)
        if (!((p[i] >= '0' && p[i] <= '9') || (p[i] >= 'a' && p[i] <= 'f')))
            return false;
    return true;
}

static void prova_installa(void)
{
    ambiente_nuovo();
    prepara_apk();
    PhAdb *a = finto_inst_adb();
    atomic_bool annulla;
    atomic_init(&annulla, false);
    PhInstRisultato r;
    PhErrore e = {{0}};

    /* 1. riuscita: copia, pm, pulizia, nell'ordine */
    Pr pr = {&annulla, 0, 0, 0, 1, 0};
    CONTROLLA(ph_inst_installa(a, pp("app.apk"), progresso, &pr, &annulla, &r, &e) == PH_INST_RIUSCITO);
    CONTROLLA(r.esito == PH_INST_RIUSCITO && !r.temporaneo_rimasto && r.messaggio[0]);
    CONTROLLA(pr.chiamate == 3 && pr.monotono);
    CONTROLLA(finto_inst_copie() == 1 && finto_inst_comandi() == 2);
    char *tmp = finto_inst_copia_remoto(0);
    CONTROLLA(tmp && nome_nostro(tmp));
    char *c0 = finto_inst_comando(0);
    char atteso[200];
    snprintf(atteso, sizeof atteso, "pm install -r '%s'", tmp ? tmp : "");
    CONTROLLA(c0 && strcmp(c0, atteso) == 0);
    free(c0);
    controlla_pulizia(tmp, 1);
    CONTROLLA(!finto_inst_remoto_esiste(tmp) && finto_inst_file_sul_telefono() == 0);
    /* il percorso del PC non finisce mai in una riga di comando */
    for (size_t i = 0; i < finto_inst_comandi(); i++) {
        char *c = finto_inst_comando(i);
        CONTROLLA(c && strstr(c, base) == NULL);
        free(c);
    }
    /* un'altra chiamata usa un altro nome */
    free(tmp);
    PhInstRisultato r2;
    CONTROLLA(ph_inst_installa(a, pp("app.apk"), NULL, NULL, NULL, &r2, NULL) == PH_INST_RIUSCITO);
    char *t1 = finto_inst_copia_remoto(0), *t2 = finto_inst_copia_remoto(1);
    CONTROLLA(t1 && t2 && strcmp(t1, t2) != 0);
    free(t1);
    free(t2);

    /* 2. non e' un APK: nessun comando, nessuna copia */
    finto_inst_reset();
    scrivi_testo(pp("falso.apk"), "ciao ciao ciao ciao ciao ciao", 29);
    CONTROLLA(ph_inst_installa(a, pp("falso.apk"), NULL, NULL, NULL, &r, &e) == PH_INST_ERRORE);
    CONTROLLA(r.motivo == PH_INST_M_APK_NON_VALIDO && r.messaggio[0] && !r.temporaneo_rimasto);
    CONTROLLA(finto_inst_comandi() == 0 && finto_inst_copie() == 0);
    CONTROLLA(ph_inst_installa(a, pp("non-esiste.apk"), NULL, NULL, NULL, &r, &e) == PH_INST_ERRORE);
    CONTROLLA(finto_inst_comandi() == 0 && finto_inst_copie() == 0);
    CONTROLLA(ph_inst_installa(NULL, pp("app.apk"), NULL, NULL, NULL, &r, &e) == PH_INST_ERRORE);
    CONTROLLA(ph_inst_installa(a, NULL, NULL, NULL, NULL, NULL, &e) == PH_INST_ERRORE);
    CONTROLLA(finto_inst_comandi() == 0 && finto_inst_copie() == 0);

    /* 3. pm rifiuta: spiegazione e pulizia */
    finto_inst_reset();
    FintoInst c = {PH_FILE_RIUSCITO, "Failure [INSTALL_FAILED_UPDATE_INCOMPATIBLE: x]\n", "", 0, 0, 0, 0};
    finto_inst_config(&c);
    CONTROLLA(ph_inst_installa(a, pp("app.apk"), NULL, NULL, NULL, &r, &e) == PH_INST_RIFIUTATO);
    CONTROLLA(r.motivo == PH_INST_M_FIRMA_DIVERSA && strcmp(r.codice, "INSTALL_FAILED_UPDATE_INCOMPATIBLE") == 0);
    tmp = finto_inst_copia_remoto(0);
    controlla_pulizia(tmp, 1);
    CONTROLLA(finto_inst_file_sul_telefono() == 0);
    free(tmp);

    /* 4. la copia fallisce (il file parziale resta "sul telefono": la pulizia lo toglie) */
    finto_inst_reset();
    c = (FintoInst){PH_FILE_ERRORE, "Success\n", "", 0, 0, 0, 0};
    finto_inst_config(&c);
    CONTROLLA(ph_inst_installa(a, pp("app.apk"), NULL, NULL, NULL, &r, &e) == PH_INST_ERRORE);
    /* la frase per l'utente non porta il dettaglio tecnico: quello e' solo in *e */
    CONTROLLA(strstr(r.messaggio, "finto") == NULL && strstr(r.messaggio, base) == NULL && r.messaggio[0]);
    CONTROLLA(strstr(e.testo, "finto: copia non riuscita") != NULL);
    CONTROLLA(finto_inst_comandi() == 1); /* niente pm */
    tmp = finto_inst_copia_remoto(0);
    controlla_pulizia(tmp, 0);
    CONTROLLA(finto_inst_file_sul_telefono() == 0 && !r.temporaneo_rimasto);
    free(tmp);

    /* 5. annullata durante la copia */
    finto_inst_reset();
    atomic_store(&annulla, false);
    c = (FintoInst){PH_FILE_RIUSCITO, "Success\n", "", 0, 0, 0, 2};
    finto_inst_config(&c);
    pr = (Pr){&annulla, 0, 0, 0, 1, 2};
    CONTROLLA(ph_inst_installa(a, pp("app.apk"), progresso, &pr, &annulla, &r, &e) == PH_INST_ANNULLATO);
    CONTROLLA(finto_inst_comandi() == 1); /* niente pm, solo la pulizia */
    tmp = finto_inst_copia_remoto(0);
    controlla_pulizia(tmp, 0);
    CONTROLLA(finto_inst_file_sul_telefono() == 0);
    free(tmp);

    /* 5b. annullata a copia finita, prima di pm */
    finto_inst_reset();
    atomic_store(&annulla, false);
    pr = (Pr){&annulla, 0, 0, 0, 1, 3};
    CONTROLLA(ph_inst_installa(a, pp("app.apk"), progresso, &pr, &annulla, &r, &e) == PH_INST_ANNULLATO);
    CONTROLLA(finto_inst_comandi() == 1 && finto_inst_file_sul_telefono() == 0);

    /* 6. la connessione cade dopo la copia: pm e pulizia non rispondono */
    finto_inst_reset();
    atomic_store(&annulla, false);
    c = (FintoInst){PH_FILE_RIUSCITO, "Success\n", "", 0, 0, 1, 0};
    finto_inst_config(&c);
    CONTROLLA(ph_inst_installa(a, pp("app.apk"), NULL, NULL, &annulla, &r, &e) == PH_INST_ERRORE);
    CONTROLLA(r.temporaneo_rimasto && nome_nostro(r.temporaneo));
    CONTROLLA(strstr(r.messaggio, "prossima connessione") != NULL); /* lo dice a chi legge */
    CONTROLLA(finto_inst_remoto_esiste(r.temporaneo));
    /* ... e cade durante la copia */
    finto_inst_reset();
    c = (FintoInst){PH_FILE_ERRORE, "", "", 0, 0, 1, 0};
    finto_inst_config(&c);
    CONTROLLA(ph_inst_installa(a, pp("app.apk"), NULL, NULL, NULL, &r, &e) == PH_INST_ERRORE);
    CONTROLLA(r.temporaneo_rimasto && strstr(r.messaggio, "prossima connessione") != NULL);

    /* 7. riuscita ma la pulizia non riesce: lo dice, l'esito resta RIUSCITO */
    finto_inst_reset();
    c = (FintoInst){PH_FILE_RIUSCITO, "Success\n", "", 0, 1, 0, 0};
    finto_inst_config(&c);
    CONTROLLA(ph_inst_installa(a, pp("app.apk"), NULL, NULL, NULL, &r, &e) == PH_INST_RIUSCITO);
    CONTROLLA(r.temporaneo_rimasto && strstr(r.messaggio, "prossima connessione") != NULL);
    /* alla connessione successiva la pulizia toglie il file rimasto */
    {
        CONTROLLA(finto_inst_remoto_esiste(r.temporaneo));
        FintoInst ok = {PH_FILE_RIUSCITO, "Success\n", "", 0, 0, 0, 0};
        finto_inst_config(&ok);
        CONTROLLA(ph_inst_pulisci(a, &e) == 0);
        CONTROLLA(finto_inst_file_sul_telefono() == 0);
        char *ult = finto_inst_comando(finto_inst_comandi() - 1);
        CONTROLLA(ult && strcmp(ult, "rm -f /data/local/tmp/yasde-apk-*.apk") == 0);
        free(ult);
        /* un temporaneo dell'installazione: lo toglie la riga; un file di altro nome resta */
        CONTROLLA(ph_file_invia(a, "x", "/data/local/tmp/yasde-apk-0123abcd.apk", NULL, NULL, NULL, &e) == PH_FILE_RIUSCITO);
        CONTROLLA(ph_file_invia(a, "x", "/data/local/tmp/yasdeX-apk-0123abcd.apk", NULL, NULL, NULL, &e) == PH_FILE_RIUSCITO);
        CONTROLLA(finto_inst_file_sul_telefono() == 2);
        CONTROLLA(ph_inst_pulisci(a, &e) == 0);
        CONTROLLA(finto_inst_file_sul_telefono() == 1);
        CONTROLLA(finto_inst_remoto_esiste("/data/local/tmp/yasdeX-apk-0123abcd.apk"));
        /* se il comando rm fallisce (codice di uscita diverso da 0) la pulizia torna errore */
        FintoInst ko = {PH_FILE_RIUSCITO, "", "", 0, 1, 0, 0};
        finto_inst_config(&ko);
        e.testo[0] = 0;
        CONTROLLA(ph_inst_pulisci(a, &e) == -1 && e.testo[0]);
        CONTROLLA(ph_inst_pulisci(NULL, NULL) == -1);
    }

    /* 8. pm non risponde: errore, e si dice che non si sa; pulizia fatta */
    finto_inst_reset();
    c = (FintoInst){PH_FILE_RIUSCITO, "Success\n", "", 1, 0, 0, 0};
    finto_inst_config(&c);
    CONTROLLA(ph_inst_installa(a, pp("app.apk"), NULL, NULL, NULL, &r, &e) == PH_INST_ERRORE);
    CONTROLLA(strstr(r.messaggio, "non so se") != NULL && !r.temporaneo_rimasto && finto_inst_file_sul_telefono() == 0);

    /* 9. risposta incomprensibile: mai "riuscito" per caso */
    finto_inst_reset();
    c = (FintoInst){PH_FILE_RIUSCITO, "boh\n", "", 0, 0, 0, 0};
    finto_inst_config(&c);
    CONTROLLA(ph_inst_installa(a, pp("app.apk"), NULL, NULL, NULL, &r, &e) == PH_INST_ERRORE);
    CONTROLLA(finto_inst_file_sul_telefono() == 0);
    finto_inst_adb_libera(a);
}

static void prova_disinstalla(void)
{
    ambiente_nuovo();
    PhAdb *a = finto_inst_adb();
    PhInstRisultato r;
    PhErrore e = {{0}};
    CONTROLLA(ph_inst_disinstalla(a, "com.example.app_1", &r, &e) == PH_INST_RIUSCITO);
    char *c = finto_inst_comando(0);
    CONTROLLA(finto_inst_comandi() == 1 && c && strcmp(c, "pm uninstall 'com.example.app_1'") == 0);
    free(c);
    /* nomi non validi: nessun comando */
    static const char *const cattivi[] = {"", "a b", "a;b", "$(x)", "a'b", "a\nb", "../x", "-k", "1abc", ".abc",
                                          "a`id`", "a|b", "a&b", "a>b", "\xc3\xa8" "x", "a/b", "a\\b", "a\"b", "a*", "a\tb"};
    finto_inst_reset();
    for (size_t i = 0; i < sizeof cattivi / sizeof *cattivi; i++) {
        CONTROLLA(!ph_inst_nome_pacchetto_valido(cattivi[i]));
        CONTROLLA(ph_inst_disinstalla(a, cattivi[i], &r, NULL) == PH_INST_ERRORE && r.messaggio[0]);
    }
    char lungo[400];
    memset(lungo, 'a', 300);
    lungo[300] = '\0';
    CONTROLLA(ph_inst_disinstalla(a, lungo, &r, NULL) == PH_INST_ERRORE);
    lungo[255] = '\0';
    CONTROLLA(ph_inst_nome_pacchetto_valido(lungo));
    lungo[256 - 1 + 1] = '\0';
    memset(lungo, 'a', 256);
    lungo[256] = '\0';
    CONTROLLA(!ph_inst_nome_pacchetto_valido(lungo));
    CONTROLLA(ph_inst_disinstalla(a, NULL, &r, NULL) == PH_INST_ERRORE);
    CONTROLLA(ph_inst_disinstalla(NULL, "com.x", &r, NULL) == PH_INST_ERRORE);
    CONTROLLA(finto_inst_comandi() == 0);
    CONTROLLA(ph_inst_nome_pacchetto_valido("com.Example.App_2") && ph_inst_nome_pacchetto_valido("a"));
    /* rifiuti */
    FintoInst cf = {PH_FILE_RIUSCITO, "Failure [DELETE_FAILED_INTERNAL_ERROR]\n", "", 0, 0, 0, 0};
    finto_inst_config(&cf);
    CONTROLLA(ph_inst_disinstalla(a, "com.android.x", &r, &e) == PH_INST_RIFIUTATO && r.motivo == PH_INST_M_SISTEMA);
    cf = (FintoInst){PH_FILE_RIUSCITO, "", "", 1, 0, 0, 0};
    finto_inst_config(&cf);
    CONTROLLA(ph_inst_disinstalla(a, "com.x", &r, &e) == PH_INST_ERRORE && strstr(r.messaggio, "non so se"));
    finto_inst_adb_libera(a);
    nftw(base, rimuovi_cb, 16, FTW_DEPTH | FTW_PHYS);
}

int main(void)
{
    prova_riconosci();
    prova_analisi();
    prova_installa();
    prova_disinstalla();
    if (base[0])
        nftw(base, rimuovi_cb, 16, FTW_DEPTH | FTW_PHYS);
    if (guasti)
        fprintf(stderr, "%d controlli falliti\n", guasti);
    else
        printf("installa: tutto ok\n");
    return guasti ? 1 : 0;
}
