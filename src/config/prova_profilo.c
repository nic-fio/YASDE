/* Prove del profilo per telefono: andata e ritorno, "non misurato", aggiornamenti,
 * tabella senza dati personali, file rovinati o di versione futura, disco.
 * Con AddressSanitizer; i testi passati al lettore stanno in buffer di
 * lunghezza esatta. HOME e XDG_* puntano a una cartella temporanea. Valori finti. */
#include <dirent.h>
#include <fcntl.h>
#include <ftw.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "config/config.h"
#include "config/profilo.h"

static int errori;

#define CONTROLLA(cond)                                                          \
    do {                                                                         \
        if (!(cond)) {                                                           \
            fprintf(stderr, "%s:%d: fallito: %s\n", __FILE__, __LINE__, #cond); \
            errori++;                                                            \
        }                                                                        \
    } while (0)

static char radice[256];
static uint32_t seme = 777;
static uint32_t casuale(void)
{
    seme ^= seme << 13;
    seme ^= seme >> 17;
    seme ^= seme << 5;
    return seme;
}

static char *copia_esatta(const char *s, size_t n)
{
    char *p = malloc(n ? n : 1);
    if (p == NULL)
        abort();
    if (n)
        memcpy(p, s, n);
    return p;
}

static int leggi(const char *t, size_t n, PhProfilo *p, PhErrore *e)
{
    char *c = copia_esatta(t, n);
    int r = ph_profilo_da_testo(c, n, p, e);
    free(c);
    return r;
}

static bool e_vuoto(const PhProfilo *p)
{
    PhProfilo *v = ph_profilo_nuovo();
    bool r = v != NULL && memcmp(p, v, sizeof *p) == 0;
    ph_profilo_libera(v);
    return r;
}

static bool uguali(const PhProfilo *a, const PhProfilo *b)
{
    if (strcmp(a->modello, b->modello) || a->api != b->api || strcmp(a->encoder, b->encoder) ||
        strcmp(a->ripiego, b->ripiego) || a->repeat_ignorato != b->repeat_ignorato || a->max_flussi != b->max_flussi ||
        a->n_sintomi != b->n_sintomi || a->n_workaround != b->n_workaround)
        return false;
    for (size_t i = 0; i < a->n_sintomi; i++)
        if (strcmp(a->sintomi[i].nome, b->sintomi[i].nome) || strcmp(a->sintomi[i].nota, b->sintomi[i].nota) ||
            strcmp(a->sintomi[i].data, b->sintomi[i].data))
            return false;
    for (size_t i = 0; i < a->n_workaround; i++)
        if (strcmp(a->workaround[i].nome, b->workaround[i].nome) || strcmp(a->workaround[i].nota, b->workaround[i].nota) ||
            strcmp(a->workaround[i].data, b->workaround[i].data))
            return false;
    return true;
}

static PhProfilo *ricco(void)
{
    PhErrore e = {{0}};
    PhProfilo *p = ph_profilo_nuovo();
    if (p == NULL)
        abort();
    CONTROLLA(ph_profilo_imposta_telefono(p, "Modello-Finto 5G (EU)", 34) == 0);
    CONTROLLA(ph_profilo_imposta_encoder(p, "c2.esempio.avc.encoder", "c2.android.avc.encoder") == 0);
    ph_profilo_imposta_repeat(p, PH_PROF_SI);
    CONTROLLA(ph_profilo_imposta_max_flussi(p, 3) == 0);
    CONTROLLA(ph_profilo_aggiungi_sintomo(p, "frame-fermi", "dopo 10 s \"fermo\"\tcon \xC3\xA8", "2026-11-24", &e) == 0);
    CONTROLLA(ph_profilo_aggiungi_sintomo(p, "colori.sbagliati", NULL, "2026-11-25", &e) == 0);
    CONTROLLA(ph_profilo_aggiungi_workaround(p, "ridisegno-forzato", "ogni 500 ms", "2026-11-24", &e) == 0);
    return p;
}

static int rm_voce(const char *p, const struct stat *st, int t, struct FTW *f)
{
    (void)st;
    (void)t;
    (void)f;
    remove(p);
    return 0;
}

static void elenco(const char *dir, char *out, size_t cap)
{
    out[0] = '\0';
    DIR *d = opendir(dir);
    if (d == NULL)
        return;
    struct dirent *v;
    while ((v = readdir(d)) != NULL)
        if (strcmp(v->d_name, ".") && strcmp(v->d_name, "..")) {
            strncat(out, v->d_name, cap - strlen(out) - 1);
            strncat(out, "|", cap - strlen(out) - 1);
        }
    closedir(d);
}

static void prova_struttura(void)
{
    PhErrore e = {{0}};
    PhBuffer b = PH_BUFFER_VUOTO, b2 = PH_BUFFER_VUOTO;
    PhProfilo *p = ricco(), *q = ph_profilo_nuovo();

    CONTROLLA(ph_profilo_in_testo(p, &b) == 0);
    CONTROLLA(leggi((char *)b.dati, b.lunghezza, q, &e) == 0 && uguali(p, q));
    CONTROLLA(ph_profilo_in_testo(q, &b2) == 0 && b.lunghezza == b2.lunghezza && memcmp(b.dati, b2.dati, b.lunghezza) == 0);
    CONTROLLA(strstr((char *)b.dati, "versione = 1") != NULL);

    /* profilo vuoto: niente inventato, andata e ritorno identico */
    ph_profilo_vuoto(p);
    ph_buffer_svuota(&b);
    CONTROLLA(ph_profilo_in_testo(p, &b) == 0);
    CONTROLLA(strstr((char *)b.dati, "modello") == NULL && strstr((char *)b.dati, "api =") == NULL &&
              strstr((char *)b.dati, "max_flussi") == NULL && strstr((char *)b.dati, "repeat") == NULL);
    CONTROLLA(leggi((char *)b.dati, b.lunghezza, q, &e) == 0 && e_vuoto(q));
    CONTROLLA(leggi("", 0, q, &e) == 0 && e_vuoto(q));

    /* "no" e' diverso da "non misurato" */
    ph_profilo_imposta_repeat(p, PH_PROF_NO);
    ph_buffer_svuota(&b);
    CONTROLLA(ph_profilo_in_testo(p, &b) == 0 && leggi((char *)b.dati, b.lunghezza, q, &e) == 0 && q->repeat_ignorato == PH_PROF_NO);

    /* setter: valori non validi non cambiano nulla */
    ph_profilo_vuoto(p);
    CONTROLLA(ph_profilo_imposta_telefono(p, "Mod|ello", 34) < 0);
    CONTROLLA(ph_profilo_imposta_telefono(p, "Modello\nX", 34) < 0);
    CONTROLLA(ph_profilo_imposta_telefono(p, "\xC3\xA8", 34) < 0);
    CONTROLLA(ph_profilo_imposta_telefono(p, "http://x.example", 34) < 0 && ph_profilo_imposta_telefono(p, "a:b", 34) < 0);
    CONTROLLA(ph_profilo_imposta_encoder(p, "enc", "a:b") < 0);
    CONTROLLA(ph_profilo_imposta_telefono(p, "ok", 1000) < 0);
    CONTROLLA(ph_profilo_imposta_telefono(p, "ok", -1) < 0);
    char lungo[200];
    memset(lungo, 'a', sizeof lungo - 1);
    lungo[sizeof lungo - 1] = '\0';
    CONTROLLA(ph_profilo_imposta_telefono(p, lungo, 34) < 0);
    CONTROLLA(ph_profilo_imposta_encoder(p, lungo, NULL) < 0);
    CONTROLLA(ph_profilo_imposta_encoder(p, "enc", lungo) < 0);
    CONTROLLA(ph_profilo_imposta_encoder(p, NULL, NULL) < 0);
    CONTROLLA(ph_profilo_imposta_max_flussi(p, 65) < 0 && ph_profilo_imposta_max_flussi(p, -1) < 0);
    CONTROLLA(e_vuoto(p));
    CONTROLLA(ph_profilo_imposta_telefono(p, "", 0) == 0 && e_vuoto(p)); /* torna a non misurato */
    CONTROLLA(ph_profilo_imposta_encoder(p, "enc", NULL) == 0 && p->ripiego[0] == '\0');
    ph_profilo_imposta_repeat(p, (PhProfTri)77);
    CONTROLLA(p->repeat_ignorato == PH_PROF_NON_MISURATO);

    /* sintomi: senza doppioni, aggiornamento, validazione, limite */
    ph_profilo_vuoto(p);
    CONTROLLA(ph_profilo_aggiungi_sintomo(p, "a", "uno", "2026-01-02", &e) == 0);
    CONTROLLA(ph_profilo_aggiungi_sintomo(p, "a", "due", "2026-03-04", &e) == 1 && p->n_sintomi == 1 &&
              strcmp(p->sintomi[0].nota, "due") == 0 && strcmp(p->sintomi[0].data, "2026-03-04") == 0);
    CONTROLLA(ph_profilo_aggiungi_workaround(p, "a", NULL, "2026-01-02", &e) == 0 && p->n_sintomi == 1 && p->n_workaround == 1);
    CONTROLLA(ph_profilo_aggiungi_sintomo(p, "b", NULL, NULL, &e) == 0 && strlen(p->sintomi[1].data) == 10 &&
              p->sintomi[1].data[4] == '-');
    CONTROLLA(ph_profilo_aggiungi_sintomo(p, "", NULL, NULL, &e) < 0 && e.testo[0] != '\0');
    CONTROLLA(ph_profilo_aggiungi_sintomo(p, "a b", NULL, NULL, &e) < 0);
    CONTROLLA(ph_profilo_aggiungi_sintomo(p, "a/b", NULL, NULL, &e) < 0);
    CONTROLLA(ph_profilo_aggiungi_sintomo(p, NULL, NULL, NULL, &e) < 0);
    CONTROLLA(ph_profilo_aggiungi_sintomo(p, lungo, NULL, NULL, &e) < 0);
    CONTROLLA(ph_profilo_aggiungi_sintomo(p, "c", lungo, NULL, &e) < 0);
    CONTROLLA(ph_profilo_aggiungi_sintomo(p, "c", NULL, "2026-13-01", &e) < 0);
    CONTROLLA(ph_profilo_aggiungi_sintomo(p, "c", NULL, "26-01-01", &e) < 0);
    CONTROLLA(ph_profilo_aggiungi_sintomo(p, "c", NULL, "2026-01-0x", &e) < 0);
    CONTROLLA(ph_profilo_aggiungi_sintomo(p, "c", NULL, "2026-02-30", &e) < 0);
    CONTROLLA(ph_profilo_aggiungi_sintomo(p, "c", NULL, "2026-02-29", &e) < 0); /* non bisestile */
    CONTROLLA(ph_profilo_aggiungi_sintomo(p, "c", NULL, "2026-04-31", &e) < 0);
    CONTROLLA(ph_profilo_aggiungi_sintomo(p, "c", NULL, "2026-00-10", &e) < 0);
    CONTROLLA(ph_profilo_aggiungi_sintomo(p, "c", NULL, "2026-01-00", &e) < 0);
    CONTROLLA(ph_profilo_aggiungi_sintomo(p, "c", NULL, "2028-02-29", &e) == 0); /* bisestile */
    CONTROLLA(ph_profilo_aggiungi_sintomo(p, "d", NULL, "2100-02-29", &e) < 0);
    p->n_sintomi--; /* toglie "c" per i conteggi che seguono */
    CONTROLLA(p->n_sintomi == 2);
    for (int i = 0; i < 40; i++) {
        char nome[16];
        snprintf(nome, sizeof nome, "s%d", i);
        int r = ph_profilo_aggiungi_sintomo(p, nome, NULL, "2026-01-01", &e);
        CONTROLLA(i < PH_PROF_MAX_VOCI - 2 ? r == 0 : r < 0);
    }
    CONTROLLA(p->n_sintomi == PH_PROF_MAX_VOCI);
    CONTROLLA(ph_profilo_aggiungi_sintomo(p, "a", "ancora", "2026-01-01", &e) == 1); /* gia' presente: anche a elenco pieno */

    ph_buffer_libera(&b);
    ph_buffer_libera(&b2);
    ph_profilo_libera(p);
    ph_profilo_libera(q);
}

static void prova_tabella(void)
{
    PhErrore e = {{0}};
    char riga[1024];
    PhProfilo *p = ricco();
    CONTROLLA(strncmp(ph_profilo_tabella_intestazione(), "| Modello |", 11) == 0);
    CONTROLLA(ph_profilo_riga_tabella(p, riga, sizeof riga, &e) == 0);
    CONTROLLA(strstr(riga, "Modello-Finto 5G (EU)") && strstr(riga, "14 (API 34)") && strstr(riga, "c2.esempio.avc.encoder") &&
              strstr(riga, "c2.android.avc.encoder") && strstr(riga, "ignorato (serve il ridisegno forzato)") &&
              strstr(riga, "| 3 |") && strstr(riga, "frame-fermi, colori.sbagliati") && strstr(riga, "ridisegno-forzato"));
    /* le note e le date non escono, mai a capo dentro la riga */
    CONTROLLA(strstr(riga, "fermo") == NULL && strstr(riga, "2026") == NULL && strchr(riga, '\n') == NULL);
    /* 8 colonne = 9 barre */
    int barre = 0;
    for (const char *c = riga; *c; c++)
        barre += *c == '|';
    CONTROLLA(barre == 9);
    /* vuoto: tutto "non misurato" */
    ph_profilo_vuoto(p);
    CONTROLLA(ph_profilo_riga_tabella(p, riga, sizeof riga, &e) == 0 && strstr(riga, "| non misurato | non misurato | non misurato | non misurato | non misurato | non misurato | nessuno registrato | nessuno registrato |") != NULL);
    p->api = 99;
    CONTROLLA(ph_profilo_riga_tabella(p, riga, sizeof riga, &e) == 0 && strstr(riga, "API 99") != NULL);
    p->api = 36;
    CONTROLLA(ph_profilo_riga_tabella(p, riga, sizeof riga, &e) == 0 && strstr(riga, "16 (API 36)") != NULL);
    ph_profilo_imposta_encoder(p, "enc", NULL);
    CONTROLLA(ph_profilo_riga_tabella(p, riga, sizeof riga, &e) == 0 && strstr(riga, "| enc | nessuno |") != NULL);
    /* buffer piccolo: errore, mai scritto oltre */
    ph_profilo_vuoto(p);
    for (size_t cap = 1; cap < 60; cap++) {
        char *piccolo = malloc(cap);
        CONTROLLA(ph_profilo_riga_tabella(p, piccolo, cap, &e) < 0);
        free(piccolo);
    }
    CONTROLLA(ph_profilo_riga_tabella(p, riga, 0, &e) < 0);
    ph_profilo_libera(p);
}

static void prova_rovinati(void)
{
    PhErrore e = {{0}};
    PhBuffer b = PH_BUFFER_VUOTO;
    PhProfilo *p = ricco(), *q = ph_profilo_nuovo();
    CONTROLLA(ph_profilo_in_testo(p, &b) == 0);
    size_t lun = b.lunghezza;
    char *testo = copia_esatta((char *)b.dati, lun);
    ph_buffer_libera(&b);

    for (size_t n = 0; n <= lun; n++) {
        if (leggi(testo, n, q, &e) < 0)
            CONTROLLA(e_vuoto(q));
    }
    for (int it = 0; it < 4000; it++) {
        char *m = malloc(lun + 8);
        memcpy(m, testo, lun);
        size_t n = lun;
        for (int j = 0; j < (int)(casuale() % 4) + 1; j++) {
            size_t pos = casuale() % n;
            if (casuale() % 3 == 0)
                m[pos] = (char)casuale();
            else if (casuale() % 2)
                m[pos] = "\"\\[]=#\n\r 0e-.\xff\xc3"[casuale() % 15];
            else {
                memmove(m + pos, m + pos + 1, n - pos - 1);
                n--;
            }
        }
        char *esatto = copia_esatta(m, n);
        if (ph_profilo_da_testo(esatto, n, q, &e) < 0)
            CONTROLLA(e_vuoto(q));
        free(esatto);
        free(m);
    }
    free(testo);

    /* versione futura: errore, profilo vuoto; versioni strane: ignorate */
    const char *futuro = "[profilo]\nversione = 2\nmodello = \"X\"\n[campo_nuovo]\nz = 1\n";
    CONTROLLA(leggi(futuro, strlen(futuro), q, &e) < 0 && strstr(e.testo, "versione 2") != NULL && e_vuoto(q));
    const char *enorme = "[profilo]\nversione = 99999999999\n";
    CONTROLLA(leggi(enorme, strlen(enorme), q, &e) < 0);
    const char *passata = "[profilo]\nversione = 0\nmodello = \"X\"\n";
    CONTROLLA(leggi(passata, strlen(passata), q, &e) == 0 && strcmp(q->modello, "X") == 0);
    const char *senza = "[profilo]\nmodello = \"Y\"\n";
    CONTROLLA(leggi(senza, strlen(senza), q, &e) == 0 && strcmp(q->modello, "Y") == 0);

    /* valori sbagliati: tornano a "non misurato" */
    const char *sbagliato =
        "[profilo]\nmodello = \"a|b\"\napi = 100000\n[encoder]\nscelto = 5\nripiego = \"\xc3\xa8\"\n"
        "repeat_previous_frame_after_ignorato = \"si\"\n[misure]\nmax_flussi = 65\n"
        "[[sintomo]]\nnome = \"ok\"\nnota = \"n\"\ndata = \"ieri\"\n"
        "[[sintomo]]\nnome = \"ok\"\n[[sintomo]]\nnome = \"nome non valido\"\n[[sintomo]]\nnota = \"senza nome\"\n"
        "[[workaround]]\nnome = 4\n[[sconosciuto]]\nx = 1\n[altro]\ny = 2\n";
    CONTROLLA(leggi(sbagliato, strlen(sbagliato), q, &e) == 0);
    CONTROLLA(q->modello[0] == '\0' && q->api == 0 && q->encoder[0] == '\0' && q->ripiego[0] == '\0' &&
              q->repeat_ignorato == PH_PROF_NON_MISURATO && q->max_flussi == 0);
    CONTROLLA(q->n_sintomi == 1 && strcmp(q->sintomi[0].nome, "ok") == 0 && strcmp(q->sintomi[0].nota, "n") == 0 &&
              q->sintomi[0].data[0] == '\0' && q->n_workaround == 0);
    /* sintassi: errore con riga */
    const char *rotto = "[profilo]\nmodello = \"aperto\n";
    CONTROLLA(leggi(rotto, strlen(rotto), q, &e) < 0 && strncmp(e.testo, "riga 2", 6) == 0 && e_vuoto(q));
    /* troppe voci: si ferma al massimo */
    {
        PhBuffer l = PH_BUFFER_VUOTO;
        char riga[64];
        for (int i = 0; i < 100; i++) {
            int k = snprintf(riga, sizeof riga, "[[sintomo]]\nnome = \"s%d\"\n[[workaround]]\nnome = \"w%d\"\n", i, i);
            ph_buffer_aggiungi(&l, riga, (size_t)k);
        }
        CONTROLLA(leggi((char *)l.dati, l.lunghezza, q, &e) == 0 && q->n_sintomi == PH_PROF_MAX_VOCI &&
                  q->n_workaround == PH_PROF_MAX_VOCI);
        ph_buffer_libera(&l);
    }
    ph_profilo_libera(p);
    ph_profilo_libera(q);
}

static void imposta_ambiente(void)
{
    char c[600], h[600];
    snprintf(c, sizeof c, "%s/cfg", radice);
    snprintf(h, sizeof h, "%s/home", radice);
    mkdir(h, 0700);
    setenv("HOME", h, 1);
    setenv("XDG_CONFIG_HOME", c, 1);
    unsetenv("XDG_CACHE_HOME");
}

static void prova_disco(void)
{
    PhErrore e = {{0}};
    PhBuffer b = PH_BUFFER_VUOTO;
    char dir[600], elen[1024], file[800];
    imposta_ambiente();
    ph_cfg_cartella_config(dir, sizeof dir, &e);
    PhProfilo *p = ricco(), *q = ph_profilo_nuovo();

    /* assente: vuoto, esito 0, nessuna cartella creata */
    struct stat st;
    CONTROLLA(ph_profilo_carica("R5CT0000000", q, &e) == 0 && e_vuoto(q) && stat(dir, &st) < 0);
    /* salva e ricarica */
    CONTROLLA(ph_profilo_salva("R5CT0000000", p, &e) == 0);
    CONTROLLA(ph_profilo_carica("R5CT0000000", q, &e) == 0 && uguali(p, q));
    elenco(dir, elen, sizeof elen);
    CONTROLLA(strstr(elen, "profilo-R5CT0000000.toml|") != NULL && strstr(elen, "|.") == NULL && elen[0] != '.');
    snprintf(file, sizeof file, "%s/profilo-R5CT0000000.toml", dir);
    CONTROLLA(stat(file, &st) == 0 && (st.st_mode & 0777) == 0600);
    /* nessun dato personale dentro il file: ne' il seriale ne' indirizzi */
    CONTROLLA(ph_cfg_leggi_file(file, 1 << 20, &b, &e) == 0 && strstr((char *)b.dati, "R5CT") == NULL);
    /* telefoni diversi, file diversi; seriale ostile resta nella cartella */
    CONTROLLA(ph_profilo_salva("AB:CD", p, &e) == 0 && ph_profilo_salva("AB.CD", q, &e) == 0);
    CONTROLLA(ph_profilo_salva("../../evil", p, &e) == 0);
    elenco(dir, elen, sizeof elen);
    CONTROLLA(strstr(elen, "profilo-______evil-") != NULL);
    CONTROLLA(ph_profilo_salva("", p, &e) < 0 && ph_profilo_carica("", q, &e) < 0);
    char sopra[700];
    snprintf(sopra, sizeof sopra, "%s", radice);
    snprintf(sopra, sizeof sopra, "%s/cfg", radice);
    elenco(sopra, elen, sizeof elen);
    CONTROLLA(strcmp(elen, "YASDE|") == 0);

    /* versione futura su disco: nessun rename, esito -1, profilo vuoto, file intatto */
    const char *futuro = "[profilo]\nversione = 7\nmodello = \"Dal-Futuro\"\n[nuova]\nx = \"y\"\n";
    CONTROLLA(ph_cfg_scrivi_file(file, futuro, strlen(futuro), &e) == 0);
    CONTROLLA(ph_profilo_carica("R5CT0000000", q, &e) == -1 && e_vuoto(q) && strstr(e.testo, "versione 7") != NULL);
    CONTROLLA(ph_cfg_leggi_file(file, 1 << 20, &b, &e) == 0 && b.lunghezza == strlen(futuro) && memcmp(b.dati, futuro, b.lunghezza) == 0);
    for (int k = 0; k < 3; k++) /* richiamarla non accumula copie */
        CONTROLLA(ph_profilo_carica("R5CT0000000", q, &e) == -1);
    elenco(dir, elen, sizeof elen);
    CONTROLLA(strstr(elen, "illeggibile") == NULL);
    char messo[820];
    snprintf(messo, sizeof messo, "%s.illeggibile", file);
    /* un programma nuovo riscrive il suo profilo valido */
    CONTROLLA(ph_profilo_salva("R5CT0000000", p, &e) == 0);
    CONTROLLA(ph_profilo_carica("R5CT0000000", q, &e) == 0 && uguali(p, q));

    /* sintassi rovinata: lo stesso */
    const char *rotto = "[profilo\nmodello";
    CONTROLLA(ph_cfg_scrivi_file(file, rotto, strlen(rotto), &e) == 0);
    CONTROLLA(ph_profilo_carica("R5CT0000000", q, &e) == 1 && e_vuoto(q) && strstr(e.testo, "riga 1") != NULL);
    CONTROLLA(ph_cfg_leggi_file(messo, 1 << 20, &b, &e) == 0 && b.lunghezza == strlen(rotto));
    /* file troppo grande: messo da parte */
    {
        int g = open(file, O_CREAT | O_WRONLY | O_TRUNC, 0600);
        if (g >= 0) {
            if (ftruncate(g, PH_PROF_MAX_FILE + 1) == 0)
                CONTROLLA(ph_profilo_carica("R5CT0000000", q, &e) == 1 && e_vuoto(q));
            close(g);
        }
    }
    ph_buffer_libera(&b);
    ph_profilo_libera(p);
    ph_profilo_libera(q);
}

int main(void)
{
    snprintf(radice, sizeof radice, "/tmp/prova_profilo_XXXXXX");
    if (mkdtemp(radice) == NULL) {
        perror("mkdtemp");
        return 2;
    }
    prova_struttura();
    prova_tabella();
    prova_rovinati();
    prova_disco();
    nftw(radice, rm_voce, 16, FTW_DEPTH | FTW_PHYS);
    if (errori == 0)
        printf("prova_profilo: tutto bene\n");
    return errori == 0 ? 0 : 1;
}
