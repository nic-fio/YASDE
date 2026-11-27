/* Prove della persistenza: lettore/scrittore di testo, strutture, percorsi,
 * scrittura sicura. Girano con AddressSanitizer; ogni testo passato al lettore
 * sta in un buffer allocato della lunghezza esatta (senza terminatore), cosi'
 * una lettura oltre la fine viene segnalata. HOME e XDG_* puntano a una
 * cartella temporanea: la vera ~/.config non si tocca mai. Tutti i valori
 * (seriali, nomi, percorsi) sono finti. */
#include <dirent.h>
#include <errno.h>
#include <math.h>
#include <fcntl.h>
#include <ftw.h>
#include <locale.h>
#include <pwd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

#include "config/config.h"
#include "config/config_interno.h"
#include "config/lingua.h"
#include "config/toml.h"

static int errori;

#define CONTROLLA(cond)                                                          \
    do {                                                                         \
        if (!(cond)) {                                                           \
            fprintf(stderr, "%s:%d: fallito: %s\n", __FILE__, __LINE__, #cond); \
            errori++;                                                            \
        }                                                                        \
    } while (0)

static void agg_s(PhBuffer *b, const char *s)
{
    if (ph_buffer_aggiungi(b, s, strlen(s)) < 0)
        abort();
}

static void invecchia(const char *percorso, long secondi);

static char radice[256]; /* cartella temporanea di tutte le prove */

/* ---------- utilita' ---------- */

static char *copia_esatta(const char *s, size_t n)
{
    char *p = malloc(n ? n : 1);
    if (p == NULL)
        abort();
    if (n)
        memcpy(p, s, n);
    return p;
}

static uint32_t seme = 12345;
static uint32_t casuale(void)
{
    seme ^= seme << 13;
    seme ^= seme >> 17;
    seme ^= seme << 5;
    return seme;
}

/* da_testo con il testo in un buffer esatto. */
static int leggi_tel(const char *testo, size_t n, PhCfgTelefono *t, PhErrore *e)
{
    char *c = copia_esatta(testo, n);
    int r = ph_cfg_telefono_da_testo(c, n, t, e);
    free(c);
    return r;
}
static int leggi_pref(const char *testo, size_t n, PhCfgPreferenze *p, PhErrore *e)
{
    char *c = copia_esatta(testo, n);
    int r = ph_cfg_preferenze_da_testo(c, n, p, e);
    free(c);
    return r;
}
static int leggi_telefoni(const char *testo, size_t n, PhCfgTelefoni *p, PhErrore *e)
{
    char *c = copia_esatta(testo, n);
    int r = ph_cfg_telefoni_da_testo(c, n, p, e);
    free(c);
    return r;
}

static bool uguali_telefono(const PhCfgTelefono *a, const PhCfgTelefono *b)
{
    if (a->sfondo.tipo != b->sfondo.tipo || a->sfondo.colore_da != b->sfondo.colore_da ||
        a->sfondo.colore_solido != b->sfondo.colore_solido || a->sfondo.colore_a != b->sfondo.colore_a || a->sfondo.direzione != b->sfondo.direzione ||
        a->sfondo.adattamento != b->sfondo.adattamento || strcmp(a->sfondo.immagine, b->sfondo.immagine) != 0)
        return false;
    if (a->n_icone != b->n_icone || a->n_widget != b->n_widget || a->n_finestre != b->n_finestre)
        return false;
    for (size_t i = 0; i < a->n_icone; i++)
        if (strcmp(a->icone[i].app, b->icone[i].app) != 0 || a->icone[i].colonna != b->icone[i].colonna ||
            a->icone[i].riga != b->icone[i].riga)
            return false;
    for (size_t i = 0; i < a->n_widget; i++)
        if (a->widget[i].tipo != b->widget[i].tipo || a->widget[i].destra != b->widget[i].destra ||
            a->widget[i].alto != b->widget[i].alto || a->widget[i].dimensione != b->widget[i].dimensione ||
            a->widget[i].larghezza != b->widget[i].larghezza || a->widget[i].altezza != b->widget[i].altezza ||
            a->widget[i].stile != b->widget[i].stile || a->widget[i].opzione != b->widget[i].opzione)
            return false;
    for (size_t i = 0; i < a->n_finestre; i++)
        if (strcmp(a->finestre[i].app, b->finestre[i].app) != 0 || a->finestre[i].x != b->finestre[i].x ||
            a->finestre[i].y != b->finestre[i].y || a->finestre[i].larghezza != b->finestre[i].larghezza ||
            a->finestre[i].altezza != b->finestre[i].altezza)
            return false;
    if (a->n_compatibilita != b->n_compatibilita)
        return false;
    for (size_t i = 0; i < a->n_compatibilita && i < PH_CFG_MAX_COMPAT; i++)
        if (strcmp(a->compatibilita[i], b->compatibilita[i]) != 0)
            return false;
    if (a->meteo_luogo_scelto != b->meteo_luogo_scelto ||
        (a->meteo_luogo_scelto && (strcmp(a->meteo_luogo_nome, b->meteo_luogo_nome) != 0 ||
                                   strcmp(a->meteo_luogo_dettaglio, b->meteo_luogo_dettaglio) != 0 ||
                                   a->meteo_luogo_lat_centesimi != b->meteo_luogo_lat_centesimi ||
                                   a->meteo_luogo_lon_centesimi != b->meteo_luogo_lon_centesimi)))
        return false;
    if (strcmp(a->note, b->note) != 0)
        return false;
    if (a->n_fissate != b->n_fissate || a->n_aperture != b->n_aperture)
        return false;
    for (size_t i = 0; i < a->n_aperture && i < PH_CFG_MAX_APERTURE; i++)
        if (strcmp(a->aperture[i].app, b->aperture[i].app) != 0 || a->aperture[i].volte != b->aperture[i].volte ||
            a->aperture[i].ultima != b->aperture[i].ultima)
            return false;
    for (size_t i = 0; i < a->n_fissate; i++)
        if (strcmp(a->fissate[i], b->fissate[i]) != 0)
            return false;
    return a->volume == b->volume &&
           strcmp(a->cartella_invio, b->cartella_invio) == 0 &&
           strcmp(a->cartella_ricezione, b->cartella_ricezione) == 0 && a->densita_app == b->densita_app;
}

static bool e_predefinito(const PhCfgTelefono *t)
{
    PhCfgTelefono *d = ph_cfg_telefono_nuovo();
    bool r = d != NULL && uguali_telefono(t, d);
    ph_cfg_telefono_libera(d);
    return r;
}

static PhCfgTelefono *telefono_ricco(void)
{
    PhCfgTelefono *t = ph_cfg_telefono_nuovo();
    if (t == NULL)
        abort();
    t->sfondo.tipo = PH_CFG_SFONDO_IMMAGINE;
    t->sfondo.colore_solido = 0x654321;
    t->sfondo.colore_da = 0x123456;
    t->sfondo.colore_a = 0xabcdef;
    t->sfondo.direzione = PH_CFG_DIR_VERTICALE;
    t->sfondo.adattamento = PH_CFG_ADATT_CENTRA;
    strcpy(t->sfondo.immagine, "sfondo-R5CT0000000.png");
    const char *app[] = {"com.example.note", "com.example.messaggi", "org.esempio.\xC3\xA8"};
    for (int i = 0; i < 3; i++) {
        strcpy(t->icone[i].app, app[i]);
        t->icone[i].colonna = i;
        t->icone[i].riga = 2 * i;
    }
    t->n_icone = 3;
    t->widget[0] = (PhCfgWidget){.tipo = PH_CFG_WIDGET_OROLOGIO, .destra = 16, .alto = 16, .dimensione = PH_CFG_WIDGET_PICCOLO, .stile = 5, .opzione = 200};
    t->widget[1] = (PhCfgWidget){.tipo = PH_CFG_WIDGET_METEO, .destra = 1000, .alto = 200, .dimensione = PH_CFG_WIDGET_GRANDE, .stile = 15};
    t->widget[2] = (PhCfgWidget){.tipo = PH_CFG_WIDGET_RIPRODUZIONE, .destra = 0, .alto = 16384, .dimensione = PH_CFG_WIDGET_MEDIO, .opzione = 255};
    t->n_widget = 3;
    t->volume = 37;
    strcpy(t->cartella_invio, "/home/utente/Immagini \"x\"\\y\ttab");
    strcpy(t->cartella_ricezione, "/home/utente/Scaricati/\xC3\xA8");
    t->densita_app = 420;
    strcpy(t->finestre[0].app, "com.example.note");
    t->finestre[0] = (PhCfgFinestra){"com.example.note", 120, 80, 400, 290};
    t->finestre[1] = (PhCfgFinestra){"com.example.messaggi", -50, -20, 296, 470};
    t->n_finestre = 2;
    strcpy(t->fissate[0], "com.example.messaggi");
    strcpy(t->fissate[1], "org.esempio.\xC3\xA8");
    strcpy(t->fissate[2], "com.example.note");
    t->n_fissate = 3;
    t->aperture[0] = (PhCfgApertura){.app = "com.example.note", .volte = 12, .ultima = 1700000000};
    t->aperture[1] = (PhCfgApertura){.app = "org.esempio.\xC3\xA8", .volte = PH_CFG_MAX_VOLTE, .ultima = 0};
    t->n_aperture = 2;
    strcpy(t->compatibilita[0], "com.example.vecchia");
    strcpy(t->compatibilita[1], "org.esempio.\xC3\xA8");
    t->n_compatibilita = 2;
    t->meteo_luogo_scelto = true;
    strcpy(t->meteo_luogo_nome, "Citt\xC3\xA0-Finta");
    strcpy(t->meteo_luogo_dettaglio, "Regione Finta");
    t->meteo_luogo_lat_centesimi = -1234;
    t->meteo_luogo_lon_centesimi = 5678;
    strcpy(t->note, "Riga uno\n\"virgolette\" e barra \\\n\tcon tab \xC3\xA8");
    return t;
}

static int rm_voce(const char *p, const struct stat *st, int tipo, struct FTW *f)
{
    (void)st;
    (void)tipo;
    (void)f;
    remove(p);
    return 0;
}

/* Elenca i nomi dei file in una cartella (senza . e ..), separati da '|'. */
static void elenco(const char *dir, char *out, size_t cap)
{
    out[0] = '\0';
    DIR *d = opendir(dir);
    if (d == NULL)
        return;
    struct dirent *v;
    while ((v = readdir(d)) != NULL) {
        if (strcmp(v->d_name, ".") == 0 || strcmp(v->d_name, "..") == 0)
            continue;
        strncat(out, v->d_name, cap - strlen(out) - 1);
        strncat(out, "|", cap - strlen(out) - 1);
    }
    closedir(d);
}

static void imposta_ambiente(const char *sotto)
{
    char p[512];
    snprintf(p, sizeof p, "%s/%s", radice, sotto);
    mkdir(p, 0700);
    char h[600];
    snprintf(h, sizeof h, "%s/home", p);
    mkdir(h, 0700);
    char c[600], k[600];
    snprintf(c, sizeof c, "%s/cfg", p);
    snprintf(k, sizeof k, "%s/cache", p);
    setenv("HOME", h, 1);
    setenv("XDG_CONFIG_HOME", c, 1);
    setenv("XDG_CACHE_HOME", k, 1);
}

/* ---------- lettore TOML ---------- */

static void prova_toml(void)
{
    PhToml doc;
    PhErrore e = {{0}};
    const char *ok = "# commento\n"
                     "radice = 1\n"
                     "[sezione]\n"
                     "s = \"a\\\"b\\\\c\\n\\t\\u00e8\\U0001F600\"   # dopo\n"
                     "i = -42\n"
                     "d = 1.5e2\r\n"
                     "b = true\n"
                     "\"chiave con spazi\" = false\n"
                     "[[el]]\n"
                     "x = 1\n"
                     "[[el]]\n"
                     "x = 2\n";
    CONTROLLA(ph_toml_leggi(ok, strlen(ok), &doc, &e) == 0);
    CONTROLLA(doc.n == 4);
    if (doc.n == 4) {
        CONTROLLA(ph_toml_voce(&doc.tabelle[0], "radice", PH_TOML_INTERO)->i == 1);
        const PhTomlTabella *s = &doc.tabelle[1];
        const PhTomlVoce *v = ph_toml_voce(s, "s", PH_TOML_STRINGA);
        CONTROLLA(v != NULL && strcmp(v->s, "a\"b\\c\n\t\xC3\xA8\xF0\x9F\x98\x80") == 0);
        CONTROLLA(ph_toml_voce(s, "i", PH_TOML_INTERO)->i == -42);
        CONTROLLA(ph_toml_voce(s, "d", PH_TOML_DECIMALE)->d == 150.0);
        CONTROLLA(ph_toml_voce(s, "b", PH_TOML_BOOLEANO)->b == true);
        CONTROLLA(ph_toml_voce(s, "i", PH_TOML_STRINGA) == NULL); /* tipo sbagliato */
        CONTROLLA(ph_toml_voce(s, "assente", PH_TOML_INTERO) == NULL);
        CONTROLLA(ph_toml_voce(s, "chiave con spazi", PH_TOML_BOOLEANO) != NULL);
        CONTROLLA(doc.tabelle[2].ripetuta && doc.tabelle[3].ripetuta);
    }
    ph_toml_libera(&doc);

    /* Errori: testo, numero di riga atteso nel messaggio. */
    struct {
        const char *testo;
        const char *riga;
    } male[] = {
        {"a = \"aperta\n", "riga 1"},
        {"\n\na = \"aperta", "riga 3"},
        {"a = 99999999999999999999\n", "riga 1"},
        {"ok = 1\nb = -99999999999999999999\n", "riga 2"},
        {"a = 1e999\n", "riga 1"},
        {"a = 1.\n", "riga 1"},
        {"a = .5\n", "riga 1"},
        {"a = 0x10\n", "riga 1"},
        {"a = nan\n", "riga 1"},
        {"a = inf\n", "riga 1"},
        {"a = 1_000\n", "riga 1"},
        {"a = [1, 2]\n", "riga 1"},
        {"a = {x = 1}\n", "riga 1"},
        {"a = 'letterale'\n", "riga 1"},
        {"a = \"\"\"multi\"\"\"\n", "riga 1"},
        {"a = 1 2\n", "riga 1"},
        {"a = true x\n", "riga 1"},
        {"a =\n", "riga 1"},
        {"a\n", "riga 1"},
        {"= 1\n", "riga 1"},
        {"a = 1\na = 2\n", "riga 2"},
        {"[t]\n[t]\n", "riga 2"},
        {"[t]\n[[t]]\n", "riga 2"},
        {"[[t]]\n[t]\n", "riga 2"},
        {"[t\n", "riga 1"},
        {"[[t]\n", "riga 1"},
        {"[]\n", "riga 1"},
        {"[a b]\n", "riga 1"},
        {"[t] x\n", "riga 1"},
        {"a = \"\\q\"\n", "riga 1"},
        {"a = \"\\u12\"\n", "riga 1"},
        {"a = \"\\u00zz\"\n", "riga 1"},
        {"a = \"\\u0000\"\n", "riga 1"},
        {"a = \"\\uD800\"\n", "riga 1"},
        {"a = \"\\U00110000\"\n", "riga 1"},
        {"a = \"x\x01y\"\n", "riga 1"},
        {"a = \"x\x7f\"\n", "riga 1"},
        {"a = \"\xff\"\n", "riga 1"},
        {"a = \"\xc0\x80\"\n", "riga 1"},           /* non minima */
        {"a = \"\xed\xa0\x80\"\n", "riga 1"},       /* surrogato */
        {"a = \"\xf4\x90\x80\x80\"\n", "riga 1"},   /* oltre U+10FFFF */
        {"a = \"\xe2\x82\"\n", "riga 1"},           /* troncato */
        {"\"\xff\" = 1\n", "riga 1"},
        {"\"\" = 1\n", "riga 1"},
        {"x\xff = 1\n", "riga 1"},
    };
    for (size_t i = 0; i < sizeof male / sizeof male[0]; i++) {
        char *c = copia_esatta(male[i].testo, strlen(male[i].testo));
        e.testo[0] = '\0';
        int r = ph_toml_leggi(c, strlen(male[i].testo), &doc, &e);
        if (r == 0) {
            fprintf(stderr, "doveva fallire: %s", male[i].testo);
            errori++;
            ph_toml_libera(&doc);
        } else {
            CONTROLLA(doc.tabelle == NULL && doc.n == 0);
            if (strncmp(e.testo, male[i].riga, strlen(male[i].riga)) != 0) {
                fprintf(stderr, "messaggio atteso '%s...' ma e' '%s' per: %s", male[i].riga, e.testo, male[i].testo);
                errori++;
            }
        }
        free(c);
    }

    /* Byte nullo: errore alla riga giusta. */
    {
        const char nulli[] = "a = 1\nb = \"x\0y\"\n";
        char *c = copia_esatta(nulli, sizeof nulli - 1);
        CONTROLLA(ph_toml_leggi(c, sizeof nulli - 1, &doc, &e) < 0);
        CONTROLLA(strncmp(e.testo, "riga 2", 6) == 0);
        free(c);
    }
    /* Riga lunghissima, file enorme. */
    {
        size_t n = PH_TOML_MAX_RIGA + 10;
        char *c = malloc(n + 8);
        memcpy(c, "a = \"", 5);
        memset(c + 5, 'x', n);
        memcpy(c + 5 + n, "\"\n", 2);
        CONTROLLA(ph_toml_leggi(c, n + 7, &doc, &e) < 0);
        CONTROLLA(strstr(e.testo, "riga 1") != NULL && strstr(e.testo, "troppo lunga") != NULL);
        /* una riga di commento lunghissima e' comunque rifiutata */
        memset(c, '#', n);
        CONTROLLA(ph_toml_leggi(c, n, &doc, &e) < 0);
        free(c);
        CONTROLLA(ph_toml_leggi("", PH_TOML_MAX_FILE + 1, &doc, &e) < 0); /* non legge il buffer */
    }
    /* Troppe tabelle, troppe voci. */
    {
        PhBuffer b = PH_BUFFER_VUOTO;
        for (int i = 0; i < 8200; i++)
            agg_s(&b, "[[t]]\n");
        CONTROLLA(ph_toml_leggi((char *)b.dati, b.lunghezza, &doc, &e) < 0 && strstr(e.testo, "troppe") != NULL);
        ph_buffer_svuota(&b);
        char riga[32];
        for (int i = 0; i < 8200; i++) {
            int k = snprintf(riga, sizeof riga, "k%d = 1\n", i);
            ph_buffer_aggiungi(&b, riga, (size_t)k);
        }
        CONTROLLA(ph_toml_leggi((char *)b.dati, b.lunghezza, &doc, &e) < 0 && strstr(e.testo, "troppe") != NULL);
        ph_buffer_libera(&b);
    }
    /* Fine riga senza a capo finale, solo spazi, CRLF. */
    {
        const char *t = "   \n\t\r\n[a]  \r\nx=1";
        CONTROLLA(ph_toml_leggi(t, strlen(t), &doc, &e) == 0 && doc.n == 2 && doc.tabelle[1].n == 1);
        ph_toml_libera(&doc);
    }

    /* Scrittura: tutto cio' che si scrive si rilegge, anche i caratteri strani. */
    {
        char tutti[140];
        size_t n = 0;
        for (int c = 1; c < 128; c++)
            tutti[n++] = (char)c;
        tutti[n++] = '\xC3';
        tutti[n++] = '\xA8';
        tutti[n++] = '\xFF'; /* non valido: diventa '?' */
        tutti[n] = '\0';
        PhBuffer b = PH_BUFFER_VUOTO;
        CONTROLLA(ph_toml_scrivi_commento(&b, "riga uno\nriga due") == 0);
        CONTROLLA(ph_toml_scrivi_stringa(&b, "s", tutti) == 0);
        CONTROLLA(ph_toml_scrivi_stringa(&b, "chiave strana \"x\"", "v") == 0);
        CONTROLLA(ph_toml_scrivi_intero(&b, "min", INT64_MIN) == 0);
        CONTROLLA(ph_toml_scrivi_intero(&b, "max", INT64_MAX) == 0);
        CONTROLLA(ph_toml_scrivi_decimale(&b, "d1", 1.0) == 0);
        CONTROLLA(ph_toml_scrivi_decimale(&b, "d2", -0.000123456789) == 0);
        CONTROLLA(ph_toml_scrivi_decimale(&b, "d3", 1e30) == 0);
        CONTROLLA(ph_toml_scrivi_decimale(&b, "d4", (double)NAN) == 0);
        CONTROLLA(ph_toml_scrivi_booleano(&b, "b", true) == 0);
        CONTROLLA(ph_toml_leggi((char *)b.dati, b.lunghezza, &doc, &e) == 0);
        if (doc.n == 1) {
            const PhTomlTabella *t = &doc.tabelle[0];
            const PhTomlVoce *v = ph_toml_voce(t, "s", PH_TOML_STRINGA);
            tutti[n - 1] = '?';
            CONTROLLA(v != NULL && strcmp(v->s, tutti) == 0);
            CONTROLLA(ph_toml_voce(t, "chiave strana \"x\"", PH_TOML_STRINGA) != NULL);
            CONTROLLA(ph_toml_voce(t, "min", PH_TOML_INTERO)->i == INT64_MIN);
            CONTROLLA(ph_toml_voce(t, "max", PH_TOML_INTERO)->i == INT64_MAX);
            CONTROLLA(ph_toml_voce(t, "d1", PH_TOML_DECIMALE)->d == 1.0);
            CONTROLLA(ph_toml_voce(t, "d3", PH_TOML_DECIMALE)->d == 1e30);
            CONTROLLA(ph_toml_voce(t, "d4", PH_TOML_DECIMALE)->d == 0.0);
            double d2 = ph_toml_voce(t, "d2", PH_TOML_DECIMALE)->d;
            CONTROLLA(d2 < -0.000123456 && d2 > -0.000123457);
        } else {
            errori++;
        }
        ph_toml_libera(&doc);
        ph_buffer_libera(&b);
    }
}

/* ---------- strutture: andata e ritorno ---------- */

static void prova_andata_ritorno(void)
{
    PhErrore e = {{0}};
    PhBuffer b = PH_BUFFER_VUOTO, b2 = PH_BUFFER_VUOTO;

    /* telefono ricco */
    PhCfgTelefono *t = telefono_ricco();
    PhCfgTelefono *r = ph_cfg_telefono_nuovo();
    CONTROLLA(ph_cfg_telefono_in_testo(t, &b) == 0);
    CONTROLLA(leggi_tel((char *)b.dati, b.lunghezza, r, &e) == 0);
    CONTROLLA(uguali_telefono(t, r));
    CONTROLLA(r->volume == 37 && r->densita_app == 420 && r->sfondo.colore_da == 0x123456 && r->sfondo.colore_solido == 0x654321);
    CONTROLLA(ph_cfg_telefono_in_testo(r, &b2) == 0);
    CONTROLLA(b.lunghezza == b2.lunghezza && memcmp(b.dati, b2.dati, b.lunghezza) == 0); /* stabile */
    ph_buffer_libera(&b2);

    /* tutte le combinazioni di enumerazioni */
    for (int ti = 0; ti < 3; ti++)
        for (int di = 0; di < 4; di++)
            for (int ai = 0; ai < 4; ai++) {
                PhCfgTelefono *x = ph_cfg_telefono_nuovo(), *y = ph_cfg_telefono_nuovo();
                x->sfondo.tipo = (PhCfgSfondoTipo)ti;
                x->sfondo.direzione = (PhCfgDirezione)di;
                x->sfondo.adattamento = (PhCfgAdattamento)ai;
                ph_buffer_svuota(&b);
                CONTROLLA(ph_cfg_telefono_in_testo(x, &b) == 0);
                CONTROLLA(leggi_tel((char *)b.dati, b.lunghezza, y, &e) == 0 && uguali_telefono(x, y));
                ph_cfg_telefono_libera(x);
                ph_cfg_telefono_libera(y);
            }
    for (int ti = 0; ti <= PH_CFG_WIDGET_TRASFERIMENTI; ti++)
        for (int di = 0; di < 3; di++) {
            PhCfgTelefono *x = ph_cfg_telefono_nuovo(), *y = ph_cfg_telefono_nuovo();
            x->widget[0] = (PhCfgWidget){.tipo = (PhCfgWidgetTipo)ti, .destra = 8 * ti, .alto = 8 * di, .dimensione = (PhCfgWidgetDim)di, .stile = ti, .opzione = 30 * di};
            x->n_widget = 1;
            ph_buffer_svuota(&b);
            CONTROLLA(ph_cfg_telefono_in_testo(x, &b) == 0);
            CONTROLLA(leggi_tel((char *)b.dati, b.lunghezza, y, &e) == 0 && uguali_telefono(x, y));
            ph_cfg_telefono_libera(x);
            ph_cfg_telefono_libera(y);
        }

    /* predefiniti: valori della specifica e del mockup */
    PhCfgTelefono *d = ph_cfg_telefono_nuovo();
    CONTROLLA(d->sfondo.tipo == PH_CFG_SFONDO_SFUMATO && d->sfondo.colore_da == 0x0e2638 &&
              d->sfondo.colore_a == 0x1a625f && d->sfondo.direzione == PH_CFG_DIR_DIAGONALE &&
              d->sfondo.adattamento == PH_CFG_ADATT_RIEMPI && d->sfondo.colore_solido == 0x1f2430 && d->volume == 100 &&
              d->densita_app == 0 && d->n_icone == 0 && d->n_finestre == 0);
    /* desktop del primo avvio: orologio, stato, notifiche, medi, a 16 px da destra */
    CONTROLLA(d->n_widget == 3 && d->widget[0].tipo == PH_CFG_WIDGET_OROLOGIO && d->widget[1].tipo == PH_CFG_WIDGET_STATO &&
              d->widget[2].tipo == PH_CFG_WIDGET_NOTIFICHE);
    for (int i = 0; i < 3 && d->n_widget == 3; i++)
        CONTROLLA(d->widget[i].destra == 16 && d->widget[i].dimensione == PH_CFG_WIDGET_MEDIO &&
                  d->widget[i].alto == (i == 0 ? 16 : i == 1 ? 120 : 240));
    /* un file vuoto da' i predefiniti */
    /* un file valido ha l'ultima parola: vuoto = nessun widget, il resto predefinito */
    CONTROLLA(leggi_tel("", 0, r, &e) == 0 && r->n_widget == 0);
    r->n_widget = 3;
    memcpy(r->widget, d->widget, sizeof r->widget);
    CONTROLLA(e_predefinito(r));
    /* e un file salvato senza widget resta senza */
    d->n_widget = 0;
    ph_buffer_svuota(&b);
    CONTROLLA(ph_cfg_telefono_in_testo(d, &b) == 0 && leggi_tel((char *)b.dati, b.lunghezza, r, &e) == 0 && r->n_widget == 0);
    ph_cfg_telefono_libera(d);

    /* preferenze */
    PhCfgPreferenze p, p2;
    ph_cfg_preferenze_predefinite(&p);
    CONTROLLA(p.lingua == PH_CFG_LINGUA_AUTO && p.esc_indietro && p.avvisi && p.scala_automatica && p.scala == 1.0 &&
              p.cartella_ricevuti[0] == '\0' && !p.widget_nascosti && !p.riapri_dove_eri);
    p.lingua = PH_CFG_LINGUA_EN;
    p.esc_indietro = false;
    p.avvisi = false;
    strcpy(p.cartella_ricevuti, "/home/utente/Ricevuti \xC3\xA8");
    p.scala_automatica = false;
    p.scala = 1.75;
    p.widget_nascosti = true;
    p.riapri_dove_eri = true;
    ph_buffer_svuota(&b);
    CONTROLLA(ph_cfg_preferenze_in_testo(&p, &b) == 0);
    CONTROLLA(leggi_pref((char *)b.dati, b.lunghezza, &p2, &e) == 0);
    CONTROLLA(p2.lingua == PH_CFG_LINGUA_EN && !p2.esc_indietro && !p2.avvisi && !p2.scala_automatica &&
              p2.scala == 1.75 && strcmp(p2.cartella_ricevuti, p.cartella_ricevuti) == 0 && p2.widget_nascosti &&
              p2.riapri_dove_eri);
    for (int l = 0; l < 3; l++) {
        p.lingua = (PhCfgLingua)l;
        ph_buffer_svuota(&b);
        CONTROLLA(ph_cfg_preferenze_in_testo(&p, &b) == 0 && leggi_pref((char *)b.dati, b.lunghezza, &p2, &e) == 0 &&
                  p2.lingua == (PhCfgLingua)l);
    }

    /* telefoni conosciuti */
    PhCfgTelefoni *ts = malloc(sizeof *ts), *ts2 = malloc(sizeof *ts2);
    ph_cfg_telefoni_predefiniti(ts);
    for (int i = 0; i < PH_CFG_MAX_TELEFONI; i++) {
        snprintf(ts->voci[i].seriale, sizeof ts->voci[i].seriale, "R5CT%07d", i);
        snprintf(ts->voci[i].nome, sizeof ts->voci[i].nome, "Telefono \"%d\" \xC3\xA8", i);
        snprintf(ts->voci[i].indirizzo, sizeof ts->voci[i].indirizzo, "192.0.2.%d:5555", i);
        ts->voci[i].visto = 1700000000 + i;
    }
    ts->n = PH_CFG_MAX_TELEFONI;
    ph_buffer_svuota(&b);
    CONTROLLA(ph_cfg_telefoni_in_testo(ts, &b) == 0);
    CONTROLLA(leggi_telefoni((char *)b.dati, b.lunghezza, ts2, &e) == 0 && ts2->n == ts->n);
    for (size_t i = 0; i < ts2->n && i < ts->n; i++)
        CONTROLLA(strcmp(ts->voci[i].seriale, ts2->voci[i].seriale) == 0 && strcmp(ts->voci[i].nome, ts2->voci[i].nome) == 0 &&
                  strcmp(ts->voci[i].indirizzo, ts2->voci[i].indirizzo) == 0 && ts->voci[i].visto == ts2->voci[i].visto);
    /* nome ostile (UTF-8 non valido, controlli): il file resta leggibile */
    ts->n = 1;
    strcpy(ts->voci[0].nome, "Cattivo\xff\xfe\x01\n\"[x]\\");
    ph_buffer_svuota(&b);
    CONTROLLA(ph_cfg_telefoni_in_testo(ts, &b) == 0);
    CONTROLLA(leggi_telefoni((char *)b.dati, b.lunghezza, ts2, &e) == 0 && ts2->n == 1);
    CONTROLLA(strncmp(ts2->voci[0].nome, "Cattivo??", 9) == 0);
    free(ts);
    free(ts2);

    ph_buffer_libera(&b);
    ph_cfg_telefono_libera(t);
    ph_cfg_telefono_libera(r);
}

/* ---------- file rovinati ---------- */

static void prova_rovinati(void)
{
    PhErrore e = {{0}};
    PhBuffer b = PH_BUFFER_VUOTO;
    PhCfgTelefono *t = telefono_ricco(), *r = ph_cfg_telefono_nuovo();
    CONTROLLA(ph_cfg_telefono_in_testo(t, &b) == 0);
    size_t lun = b.lunghezza;
    char *testo = copia_esatta((char *)b.dati, lun);
    ph_buffer_libera(&b);

    /* troncato a ogni byte: mai crash; se errore, i predefiniti e "riga N" */
    for (size_t n = 0; n <= lun; n++) {
        e.testo[0] = '\0';
        int rr = leggi_tel(testo, n, r, &e);
        if (rr < 0) {
            CONTROLLA(strncmp(e.testo, "riga ", 5) == 0);
            CONTROLLA(e_predefinito(r));
        }
    }
    /* Il troncamento a meta' di un numero o di true/false puo' dare un file
     * "valido": va bene, basta che non crashi. Interi: valido. */
    CONTROLLA(leggi_tel(testo, lun, r, &e) == 0 && uguali_telefono(t, r));

    /* byte alterati, inseriti, cancellati, a caso (ripetibile) */
    for (int it = 0; it < 4000; it++) {
        size_t n = lun;
        char *m = malloc(lun + 8);
        memcpy(m, testo, lun);
        int k = (int)(casuale() % 4) + 1;
        for (int j = 0; j < k; j++) {
            size_t pos = casuale() % (n ? n : 1);
            switch (casuale() % 4) {
            case 0: m[pos] = (char)casuale(); break;
            case 1: m[pos] = "\"\\[]=#\n\r 0e-.\xff\xc3"[casuale() % 15]; break;
            case 2: /* cancella un byte */
                memmove(m + pos, m + pos + 1, n - pos - 1);
                n--;
                break;
            default: /* inserisce un byte */
                if (n < lun + 4) {
                    memmove(m + pos + 1, m + pos, n - pos);
                    m[pos] = (char)casuale();
                    n++;
                }
            }
        }
        char *esatto = copia_esatta(m, n);
        int rr = ph_cfg_telefono_da_testo(esatto, n, r, &e);
        if (rr < 0)
            CONTROLLA(e_predefinito(r));
        PhCfgPreferenze p;
        PhCfgTelefoni *ts = malloc(sizeof *ts);
        (void)ph_cfg_preferenze_da_testo(esatto, n, &p, &e);
        (void)ph_cfg_telefoni_da_testo(esatto, n, ts, &e);
        free(ts);
        free(esatto);
        free(m);
    }
    free(testo);

    /* casi scritti a mano */
    struct {
        const char *testo;
        int esito; /* 0 letto, -1 errore */
    } casi[] = {
        {"[audio]\nvolume = 101\n", 0},
        {"[audio]\nvolume = -1\n", 0},
        {"[audio]\nvolume = \"alto\"\n", 0},
        {"[audio]\nvolume = 55.5\n", 0},
        {"[audio]\nvolume = 99999999999999999999\n", -1},
        {"[app]\ndensita = 5\n", 0},
        {"[app]\ndensita = 100000\n", 0},
        {"[sfondo]\ncolore_da = \"rosso\"\ncolore_a = \"#12345\"\ndirezione = \"su\"\ntipo = 3\n", 0},
        {"[sfondo]\ncolore_da = \"#ggg000\"\nadattamento = \"?\"\nimmagine = \"../../etc/passwd\"\n", 0},
        {"[[icona]]\napp = \"a\"\n[[icona]]\napp = \"b\"\ncolonna = 300\nriga = 0\n[[icona]]\ncolonna = 1\nriga = 1\n", 0},
        {"[[widget]]\ntipo = \"sconosciuto\"\n[[widget]]\ndestra = 5\n[[widget]]\ntipo = \"meteo\"\ndestra = -5\nalto = 99999\ndimensione = \"enorme\"\n", 0},
        {"[[finestra]]\napp = \"a\"\nx = 1\ny = 2\nlarghezza = 0\naltezza = 5\n", 0},
        {"[futuro]\nqualcosa = [1,2]\n", -1}, /* sintassi non supportata: errore anche in una sezione sconosciuta */
        {"[futuro]\nqualcosa = 1\n[[altro]]\nx = \"y\"\nsfondo = 3\n", 0},
        {"sfondo = 1\naudio = 2\n", 0},
        {"[audio]\nvolume = 10\nvolume = 20\n", -1},
    };
    for (size_t i = 0; i < sizeof casi / sizeof casi[0]; i++) {
        int rr = leggi_tel(casi[i].testo, strlen(casi[i].testo), r, &e);
        if (rr != casi[i].esito) {
            fprintf(stderr, "caso %zu: esito %d invece di %d\n", i, rr, casi[i].esito);
            errori++;
        }
        if (rr < 0)
            CONTROLLA(e_predefinito(r));
    }
    /* valori fuori intervallo -> predefiniti; il resto del file si legge */
    CONTROLLA(leggi_tel("[audio]\nvolume = 101\nmuto = true\n", strlen("[audio]\nvolume = 101\nmuto = true\n"), r, &e) == 0 && r->volume == 100);
    /* Sotto il minimo: predefinito (100), non un taglio a 0. Con il predefinito a 100 il caso 101 qui sopra
     * da solo non distinguerebbe piu' «torna al predefinito» da «taglia al limite». */
    CONTROLLA(leggi_tel("[audio]\nvolume = -1\n", strlen("[audio]\nvolume = -1\n"), r, &e) == 0 && r->volume == 100);
    CONTROLLA(leggi_tel("[audio]\nvolume = 0\n", strlen("[audio]\nvolume = 0\n"), r, &e) == 0 && r->volume == 0);
    CONTROLLA(leggi_tel("[audio]\nvolume = 100\n", strlen("[audio]\nvolume = 100\n"), r, &e) == 0 && r->volume == 100);
    CONTROLLA(leggi_tel("[app]\ndensita = 5\n", strlen("[app]\ndensita = 5\n"), r, &e) == 0 && r->densita_app == 0);
    CONTROLLA(leggi_tel("[app]\ndensita = 72\n", strlen("[app]\ndensita = 72\n"), r, &e) == 0 && r->densita_app == 72);
    CONTROLLA(leggi_tel("[app]\ndensita = 960\n", strlen("[app]\ndensita = 960\n"), r, &e) == 0 && r->densita_app == 960);
    CONTROLLA(leggi_tel("[app]\ndensita = 961\n", strlen("[app]\ndensita = 961\n"), r, &e) == 0 && r->densita_app == 0);
    {
        const char *s = "[sfondo]\ntipo = \"solido\"\ncolore_da = \"#FFaa00\"\ncolore_a = \"#12345\"\n"
                        "immagine = \"../../etc/passwd\"\nnuova = 1\n[sconosciuta]\nx = 1\n";
        CONTROLLA(leggi_tel(s, strlen(s), r, &e) == 0 && r->sfondo.tipo == PH_CFG_SFONDO_SOLIDO &&
                  r->sfondo.colore_da == 0xffaa00 && r->sfondo.colore_a == 0x1a625f && r->sfondo.immagine[0] == '\0');
    }
    {
        const char *s = "[[icona]]\napp = \"a\"\ncolonna = 1\nriga = 2\n"
                        "[[icona]]\napp = \"a\"\ncolonna = 5\nriga = 5\n"       /* doppia */
                        "[[icona]]\napp = \"b\"\ncolonna = 300\nriga = 0\n"     /* fuori intervallo */
                        "[[icona]]\napp = \"\"\ncolonna = 0\nriga = 0\n"        /* vuota */
                        "[[icona]]\ncolonna = 1\nriga = 1\n"                    /* senza app */
                        "[[icona]]\napp = \"c\"\ncolonna = 255\nriga = 0\n"
                        "[[widget]]\ntipo = \"sconosciuto\"\n"
                        "[[widget]]\ndestra = 5\n"
                        "[[widget]]\ntipo = \"meteo\"\ndestra = -5\nalto = 99999\ndimensione = \"enorme\"\n";
        CONTROLLA(leggi_tel(s, strlen(s), r, &e) == 0);
        CONTROLLA(r->n_icone == 2 && strcmp(r->icone[0].app, "a") == 0 && r->icone[0].colonna == 1 &&
                  strcmp(r->icone[1].app, "c") == 0);
        CONTROLLA(r->n_widget == 1 && r->widget[0].tipo == PH_CFG_WIDGET_METEO && r->widget[0].destra == 16 &&
                  r->widget[0].alto == 16 && r->widget[0].dimensione == PH_CFG_WIDGET_MEDIO);
    }
    {
        /* stringhe troppo lunghe: scartate, non troncate */
        PhBuffer l = PH_BUFFER_VUOTO;
        agg_s(&l, "[cartelle]\ninvio = \"");
        for (int i = 0; i < 2000; i++)
            agg_s(&l, "x");
        agg_s(&l, "\"\nricezione = \"/ok\"\n[[icona]]\napp = \"");
        for (int i = 0; i < 300; i++)
            agg_s(&l, "y");
        agg_s(&l, "\"\ncolonna = 0\nriga = 0\n");
        CONTROLLA(leggi_tel((char *)l.dati, l.lunghezza, r, &e) == 0);
        CONTROLLA(r->cartella_invio[0] == '\0' && strcmp(r->cartella_ricezione, "/ok") == 0 && r->n_icone == 0);
        ph_buffer_libera(&l);
    }
    {
        /* troppe voci: si fermano al massimo */
        PhBuffer l = PH_BUFFER_VUOTO;
        char riga[128];
        for (int i = 0; i < 300; i++) {
            int k = snprintf(riga, sizeof riga, "[[icona]]\napp = \"app%d\"\ncolonna = %d\nriga = 0\n", i, i % 256);
            ph_buffer_aggiungi(&l, riga, (size_t)k);
        }
        for (int i = 0; i < 100; i++) {
            int k = snprintf(riga, sizeof riga, "[[widget]]\ntipo = \"note\"\n[[finestra]]\napp = \"f%d\"\nx = 0\ny = 0\n"
                                                "larghezza = 10\naltezza = 10\n", i);
            ph_buffer_aggiungi(&l, riga, (size_t)k);
        }
        CONTROLLA(leggi_tel((char *)l.dati, l.lunghezza, r, &e) == 0);
        CONTROLLA(r->n_icone == PH_CFG_MAX_ICONE && r->n_widget == PH_CFG_MAX_WIDGET && r->n_finestre == PH_CFG_MAX_FINESTRE);
        ph_buffer_libera(&l);
    }
    {
        /* telefoni: seriale vuoto, troppo lungo, doppio; visto assurdo */
        PhCfgTelefoni *ts = malloc(sizeof *ts);
        PhBuffer l = PH_BUFFER_VUOTO;
        agg_s(&l, "[[telefono]]\nseriale = \"\"\n[[telefono]]\nnome = \"senza seriale\"\n[[telefono]]\nseriale = \"");
        for (int i = 0; i < 64; i++)
            agg_s(&l, "z");
        agg_s(&l, "\"\n[[telefono]]\nseriale = \"R5CT0000001\"\nvisto = -5\n"
                               "[[telefono]]\nseriale = \"R5CT0000001\"\nnome = \"doppio\"\n"
                               "[[telefono]]\nseriale = \"R5CT0000002\"\nvisto = 99999999999\n");
        CONTROLLA(leggi_telefoni((char *)l.dati, l.lunghezza, ts, &e) == 0);
        CONTROLLA(ts->n == 2 && strcmp(ts->voci[0].seriale, "R5CT0000001") == 0 && ts->voci[0].visto == 0 &&
                  ts->voci[0].nome[0] == '\0' && ts->voci[1].visto == 0);
        ph_buffer_libera(&l);
        free(ts);
    }
    {
        PhCfgPreferenze p;
        const char *s = "[generale]\nlingua = \"klingon\"\nesc_indietro = 3\navvisi = false\n[shell]\nscala = 9.5\n";
        CONTROLLA(leggi_pref(s, strlen(s), &p, &e) == 0 && p.lingua == PH_CFG_LINGUA_AUTO && p.esc_indietro && !p.avvisi &&
                  p.scala == 1.0);
        s = "[shell]\nscala = 2\nscala_automatica = false\n";
        CONTROLLA(leggi_pref(s, strlen(s), &p, &e) == 0 && p.scala == 2.0 && !p.scala_automatica);
        s = "[shell]\nscala = 0.1\n";
        CONTROLLA(leggi_pref(s, strlen(s), &p, &e) == 0 && p.scala == 1.0);
        s = "[generale]\ncartella_ricevuti = \"\xff\"\n";
        CONTROLLA(leggi_pref(s, strlen(s), &p, &e) < 0);
    }
    ph_cfg_telefono_libera(t);
    ph_cfg_telefono_libera(r);
}

/* ---------- percorsi ---------- */

static void prova_percorsi(void)
{
    PhErrore e = {{0}};
    char out[PH_CFG_LUN_PERCORSO + 64], atteso[800];

    imposta_ambiente("percorsi");
    snprintf(atteso, sizeof atteso, "%s/percorsi/cfg/YASDE", radice);
    CONTROLLA(ph_cfg_cartella_config(out, sizeof out, &e) == 0 && strcmp(out, atteso) == 0);
    snprintf(atteso, sizeof atteso, "%s/percorsi/cache/YASDE", radice);
    CONTROLLA(ph_cfg_cartella_cache(out, sizeof out, &e) == 0 && strcmp(out, atteso) == 0);
    snprintf(atteso, sizeof atteso, "%s/percorsi/cfg/YASDE/preferenze.toml", radice);
    CONTROLLA(ph_cfg_percorso_preferenze(out, sizeof out, &e) == 0 && strcmp(out, atteso) == 0);
    snprintf(atteso, sizeof atteso, "%s/percorsi/cfg/YASDE/telefoni.toml", radice);
    CONTROLLA(ph_cfg_percorso_telefoni(out, sizeof out, &e) == 0 && strcmp(out, atteso) == 0);
    snprintf(atteso, sizeof atteso, "%s/percorsi/cfg/YASDE/telefono-R5CT0000000.toml", radice);
    CONTROLLA(ph_cfg_percorso_telefono("R5CT0000000", out, sizeof out, &e) == 0 && strcmp(out, atteso) == 0);
    snprintf(atteso, sizeof atteso, "%s/percorsi/cache/YASDE/sfondo-1.png", radice);
    CONTROLLA(ph_cfg_percorso_cache("sfondo-1.png", out, sizeof out, &e) == 0 && strcmp(out, atteso) == 0);

    /* XDG con '/' finale, vuoto o relativo; senza HOME */
    char c[600];
    snprintf(c, sizeof c, "%s/percorsi/cfg/", radice);
    setenv("XDG_CONFIG_HOME", c, 1);
    snprintf(atteso, sizeof atteso, "%s/percorsi/cfg/YASDE", radice);
    CONTROLLA(ph_cfg_cartella_config(out, sizeof out, &e) == 0 && strcmp(out, atteso) == 0);
    setenv("XDG_CONFIG_HOME", "", 1);
    snprintf(atteso, sizeof atteso, "%s/percorsi/home/.config/YASDE", radice);
    CONTROLLA(ph_cfg_cartella_config(out, sizeof out, &e) == 0 && strcmp(out, atteso) == 0);
    setenv("XDG_CONFIG_HOME", "relativo/non/valido", 1);
    CONTROLLA(ph_cfg_cartella_config(out, sizeof out, &e) == 0 && strcmp(out, atteso) == 0);
    unsetenv("XDG_CACHE_HOME");
    snprintf(atteso, sizeof atteso, "%s/percorsi/home/.cache/YASDE", radice);
    CONTROLLA(ph_cfg_cartella_cache(out, sizeof out, &e) == 0 && strcmp(out, atteso) == 0);
    /* troppo lungo per il buffer: errore, non troncamento */
    CONTROLLA(ph_cfg_cartella_config(out, 10, &e) < 0);
    CONTROLLA(ph_cfg_percorso_telefono("R5CT0000000", out, 40, &e) < 0);
    imposta_ambiente("percorsi");

    /* seriale ripulito */
    char s[PH_CFG_LUN_SERIALE_FILE];
    CONTROLLA(ph_cfg_seriale_pulito("R5CT0000000", s, sizeof s, &e) == 0 && strcmp(s, "R5CT0000000") == 0);
    CONTROLLA(ph_cfg_seriale_pulito("a-b_c", s, sizeof s, &e) == 0 && strcmp(s, "a-b_c") == 0);
    /* se cambia qualcosa si aggiunge "-" e 8 cifre esadecimali */
    CONTROLLA(ph_cfg_seriale_pulito("../../etc/passwd", s, sizeof s, &e) == 0 && strncmp(s, "______etc_passwd-", 17) == 0 &&
              strlen(s) == 25 && strspn(s + 17, "0123456789abcdef") == 8);
    CONTROLLA(ph_cfg_seriale_pulito("\xc3\xa8\xff\n", s, sizeof s, &e) == 0 && strncmp(s, "____-", 5) == 0);
    CONTROLLA(ph_cfg_seriale_pulito("..", s, sizeof s, &e) == 0 && strncmp(s, "__-", 3) == 0);
    CONTROLLA(ph_cfg_seriale_pulito("", s, sizeof s, &e) < 0);
    CONTROLLA(ph_cfg_seriale_pulito(NULL, s, sizeof s, &e) < 0);
    CONTROLLA(ph_cfg_seriale_pulito("x", s, PH_CFG_LUN_SERIALE_FILE - 1, &e) < 0);
    char lungo[500];
    memset(lungo, 'q', sizeof lungo - 1);
    lungo[sizeof lungo - 1] = '\0';
    CONTROLLA(ph_cfg_seriale_pulito(lungo, s, sizeof s, &e) == 0 && strlen(s) == 63 + 9);
    /* seriali diversi che prima collidevano: file diversi, e stabili */
    {
        const char *simili[] = {"AB:CD", "AB.CD", "AB_CD", "AB CD", "AB/CD", "ab:cd", "AB:CD "};
        char p[8][700];
        for (size_t i = 0; i < 7; i++)
            CONTROLLA(ph_cfg_percorso_telefono(simili[i], p[i], sizeof p[i], &e) == 0);
        for (size_t i = 0; i < 7; i++)
            for (size_t k = i + 1; k < 7; k++)
                CONTROLLA(strcmp(p[i], p[k]) != 0);
        CONTROLLA(ph_cfg_percorso_telefono("AB:CD", p[7], sizeof p[7], &e) == 0 && strcmp(p[0], p[7]) == 0);
        /* uguali nei primi 64 caratteri */
        char l1[200], l2[200], p1[700], p2[700];
        memset(l1, 'k', 100);
        l1[100] = '\0';
        memcpy(l2, l1, 101);
        l2[99] = 'z';
        CONTROLLA(ph_cfg_percorso_telefono(l1, p1, sizeof p1, &e) == 0 && ph_cfg_percorso_telefono(l2, p2, sizeof p2, &e) == 0 &&
                  strcmp(p1, p2) != 0);
    }
    /* chiave ADB */
    snprintf(atteso, sizeof atteso, "%s/percorsi/cfg/YASDE/chiave.pem", radice);
    CONTROLLA(ph_cfg_percorso_chiave(out, sizeof out, &e) == 0 && strcmp(out, atteso) == 0);

    /* nessun seriale ostile esce dalla cartella o crea sottocartelle */
    const char *ostili[] = {"../../../etc/passwd", "/etc/passwd", "a/../b", "..", ".", "\xff\xfe", "x\ny", "a b", lungo, "~", "-rf"};
    for (size_t i = 0; i < sizeof ostili / sizeof ostili[0]; i++) {
        CONTROLLA(ph_cfg_percorso_telefono(ostili[i], out, sizeof out, &e) == 0);
        char dir[600];
        ph_cfg_cartella_config(dir, sizeof dir, &e);
        size_t ld = strlen(dir);
        CONTROLLA(strncmp(out, dir, ld) == 0 && strncmp(out + ld, "/telefono-", 10) == 0 && strchr(out + ld + 1, '/') == NULL &&
                  strstr(out, "..") == NULL && strlen(out + ld) < 100);
    }

    /* nomi nella cache */
    CONTROLLA(ph_cfg_nome_cache_valido("sfondo-R5CT0000000.png"));
    CONTROLLA(ph_cfg_nome_cache_valido("a"));
    CONTROLLA(!ph_cfg_nome_cache_valido(""));
    CONTROLLA(!ph_cfg_nome_cache_valido(NULL));
    CONTROLLA(!ph_cfg_nome_cache_valido(".nascosto"));
    CONTROLLA(!ph_cfg_nome_cache_valido(".."));
    CONTROLLA(!ph_cfg_nome_cache_valido("a/b"));
    CONTROLLA(!ph_cfg_nome_cache_valido("a b"));
    CONTROLLA(!ph_cfg_nome_cache_valido("\xc3\xa8"));
    CONTROLLA(!ph_cfg_nome_cache_valido(lungo));
    CONTROLLA(ph_cfg_percorso_cache("../x", out, sizeof out, &e) < 0);
}

/* ---------- file: lettura, scrittura sicura ---------- */

static void prova_file(void)
{
    PhErrore e = {{0}};
    PhBuffer b = PH_BUFFER_VUOTO;
    char dir[600], elen[1024], percorso[700];

    /* ambiente pulito: i file non esistono ancora */
    imposta_ambiente("file");
    PhCfgTelefono *t = telefono_ricco(), *r = ph_cfg_telefono_nuovo();
    PhCfgPreferenze p;
    CONTROLLA(ph_cfg_preferenze_carica(&p, &e) == 0 && p.lingua == PH_CFG_LINGUA_AUTO); /* assente = predefiniti */
    PhCfgTelefoni *ts = malloc(sizeof *ts);
    CONTROLLA(ph_cfg_telefoni_carica(ts, &e) == 0 && ts->n == 0);
    CONTROLLA(ph_cfg_telefono_carica("R5CT0000000", r, &e) == 0 && e_predefinito(r));
    ph_cfg_cartella_config(dir, sizeof dir, &e);
    struct stat st;
    CONTROLLA(stat(dir, &st) < 0); /* leggere non crea nulla */

    /* salva e ricarica */
    CONTROLLA(ph_cfg_telefono_salva("R5CT0000000", t, &e) == 0);
    CONTROLLA(ph_cfg_telefono_carica("R5CT0000000", r, &e) == 0 && uguali_telefono(t, r));
    CONTROLLA(stat(dir, &st) == 0 && S_ISDIR(st.st_mode) && (st.st_mode & 0777) == 0700);
    ph_cfg_percorso_telefono("R5CT0000000", percorso, sizeof percorso, &e);
    CONTROLLA(stat(percorso, &st) == 0 && S_ISREG(st.st_mode) && (st.st_mode & 0777) == 0600);

    p.lingua = PH_CFG_LINGUA_IT;
    p.scala = 2.5;
    p.scala_automatica = false;
    CONTROLLA(ph_cfg_preferenze_salva(&p, &e) == 0);
    PhCfgPreferenze p2;
    CONTROLLA(ph_cfg_preferenze_carica(&p2, &e) == 0 && p2.lingua == PH_CFG_LINGUA_IT && p2.scala == 2.5);
    ts->n = 2;
    strcpy(ts->voci[0].seriale, "R5CT0000000");
    strcpy(ts->voci[1].seriale, "R5CT0000001");
    CONTROLLA(ph_cfg_telefoni_salva(ts, &e) == 0);
    PhCfgTelefoni *ts2 = malloc(sizeof *ts2);
    CONTROLLA(ph_cfg_telefoni_carica(ts2, &e) == 0 && ts2->n == 2);
    free(ts2);

    /* nessun file temporaneo rimasto, nient'altro nella cartella */
    elenco(dir, elen, sizeof elen);
    CONTROLLA(strstr(elen, "preferenze.toml|") && strstr(elen, "telefoni.toml|") && strstr(elen, "telefono-R5CT0000000.toml|"));
    CONTROLLA(strchr(elen, '.') == NULL || strstr(elen, "|.") == NULL);
    CONTROLLA(elen[0] != '.');

    /* un seriale ostile finisce in un file dentro la cartella, e basta */
    CONTROLLA(ph_cfg_telefono_salva("../../evil/../x", t, &e) == 0);
    elenco(dir, elen, sizeof elen);
    CONTROLLA(strstr(elen, "telefono-______evil____x-") != NULL);
    char sopra[700];
    snprintf(sopra, sizeof sopra, "%s/file/cfg", radice);
    elenco(sopra, elen, sizeof elen);
    CONTROLLA(strcmp(elen, "YASDE|") == 0);
    CONTROLLA(ph_cfg_telefono_salva("", t, &e) < 0);
    CONTROLLA(ph_cfg_telefono_carica("", r, &e) < 0 && e_predefinito(r));

    /* file rovinato su disco: messo da parte, predefiniti, esito 1 */
    const char *rotto = "[audio]\nvolume = 5\nmuto = \"mezzo";
    ph_cfg_percorso_telefono("R5CT0000000", percorso, sizeof percorso, &e);
    char messo[760];
    snprintf(messo, sizeof messo, "%s.illeggibile", percorso);
    CONTROLLA(ph_cfg_scrivi_file(messo, "vecchio", 7, &e) == 0); /* un precedente da sostituire */
    CONTROLLA(ph_cfg_scrivi_file(percorso, rotto, strlen(rotto), &e) == 0);
    CONTROLLA(ph_cfg_telefono_carica("R5CT0000000", r, &e) == 1 && strstr(e.testo, "riga 3") != NULL &&
              strstr(e.testo, "da parte") != NULL && e_predefinito(r));
    CONTROLLA(stat(percorso, &st) < 0);
    /* il precedente non e' stato schiacciato: la nuova copia e' ".illeggibile.1" */
    CONTROLLA(ph_cfg_leggi_file(messo, 1 << 20, &b, &e) == 0 && b.lunghezza == 7 && memcmp(b.dati, "vecchio", 7) == 0);
    char messo1[780];
    snprintf(messo1, sizeof messo1, "%s.1", messo);
    CONTROLLA(ph_cfg_leggi_file(messo1, 1 << 20, &b, &e) == 0 && b.lunghezza == strlen(rotto) && memcmp(b.dati, rotto, b.lunghezza) == 0);
    /* ora il file non c'e': secondo caricamento = primo avvio, e salvare non distrugge il messo da parte */
    CONTROLLA(ph_cfg_telefono_carica("R5CT0000000", r, &e) == 0 && e_predefinito(r));
    CONTROLLA(ph_cfg_telefono_salva("R5CT0000000", t, &e) == 0);
    CONTROLLA(ph_cfg_leggi_file(messo1, 1 << 20, &b, &e) == 0 && b.lunghezza == strlen(rotto));
    /* lo stesso per preferenze e telefoni */
    char pp[700];
    ph_cfg_percorso_preferenze(pp, sizeof pp, &e);
    CONTROLLA(ph_cfg_scrivi_file(pp, "[generale\n", 10, &e) == 0);
    CONTROLLA(ph_cfg_preferenze_carica(&p2, &e) == 1 && p2.lingua == PH_CFG_LINGUA_AUTO);
    ph_cfg_percorso_telefoni(pp, sizeof pp, &e);
    CONTROLLA(ph_cfg_scrivi_file(pp, "x", 1, &e) == 0);
    CONTROLLA(ph_cfg_telefoni_carica(ts, &e) == 1 && ts->n == 0);
    ph_cfg_preferenze_salva(&p, &e);
    ph_cfg_telefoni_salva(ts, &e);
    /* file troppo grande: messo da parte anche lui */
    {
        int g = open(percorso, O_CREAT | O_WRONLY | O_TRUNC, 0600);
        if (g >= 0) {
            if (ftruncate(g, 3 * 1024 * 1024) == 0)
                CONTROLLA(ph_cfg_telefono_carica("R5CT0000000", r, &e) == 1 && e_predefinito(r));
            close(g);
        }
        ph_cfg_telefono_salva("R5CT0000000", t, &e);
    }
    /* nomi esauriti: si sostituisce il piu' vecchio (qui ".illeggibile.5") */
    for (int k = 0; k < 10; k++) {
        char nm[800];
        if (k == 0)
            snprintf(nm, sizeof nm, "%s", messo);
        else
            snprintf(nm, sizeof nm, "%s.%d", messo, k);
        ph_cfg_scrivi_file(nm, "x", 1, &e);
        invecchia(nm, k == 5 ? 90000 : 1000 + k);
    }
    CONTROLLA(ph_cfg_scrivi_file(percorso, rotto, strlen(rotto), &e) == 0);
    CONTROLLA(ph_cfg_telefono_carica("R5CT0000000", r, &e) == 1 && stat(percorso, &st) < 0);
    {
        char nm5[800];
        snprintf(nm5, sizeof nm5, "%s.5", messo);
        CONTROLLA(ph_cfg_leggi_file(nm5, 1 << 20, &b, &e) == 0 && b.lunghezza == strlen(rotto));
        CONTROLLA(ph_cfg_leggi_file(messo, 1 << 20, &b, &e) == 0 && b.lunghezza == 1);
    }
    for (int k = 0; k < 10; k++) {
        char nm[800];
        if (k == 0)
            snprintf(nm, sizeof nm, "%s", messo);
        else
            snprintf(nm, sizeof nm, "%s.%d", messo, k);
        unlink(nm);
    }
    ph_cfg_telefono_salva("R5CT0000000", t, &e);

    /* lettura di file particolari */
    CONTROLLA(ph_cfg_leggi_file("/nonesiste/proprio/no", 100, &b, &e) == 1 && b.lunghezza == 0);
    CONTROLLA(ph_cfg_leggi_file(dir, 100, &b, &e) < 0); /* una cartella */
    CONTROLLA(ph_cfg_leggi_file(percorso, 5, &b, &e) == 2 && strstr(e.testo, "troppo grande") != NULL && b.lunghezza == 0);
    char fifo[700];
    snprintf(fifo, sizeof fifo, "%s/fifo", dir);
    if (mkfifo(fifo, 0600) == 0)
        CONTROLLA(ph_cfg_leggi_file(fifo, 100, &b, &e) < 0); /* non si blocca */
    char grande[700];
    snprintf(grande, sizeof grande, "%s/grande", dir);
    int fd = open(grande, O_CREAT | O_WRONLY, 0600);
    if (fd >= 0) {
        if (ftruncate(fd, 3 * 1024 * 1024) == 0) /* file sparso */
            CONTROLLA(ph_cfg_leggi_file(grande, PH_CFG_MAX_FILE, &b, &e) == 2);
        close(fd);
    }
    unlink(fifo);
    unlink(grande);

    /* scrittura: un percorso sotto un file normale, percorsi non validi */
    char sotto[700];
    snprintf(sotto, sizeof sotto, "%s/preferenze.toml/x/y.toml", dir);
    CONTROLLA(ph_cfg_scrivi_file(sotto, "a", 1, &e) < 0 && e.testo[0] != '\0');
    CONTROLLA(ph_cfg_scrivi_file("", "a", 1, &e) < 0);
    CONTROLLA(ph_cfg_scrivi_file("senza_barra", "a", 1, &e) < 0);
    snprintf(sotto, sizeof sotto, "%s/", dir);
    CONTROLLA(ph_cfg_scrivi_file(sotto, "a", 1, &e) < 0);

    /* disco pieno simulato (limite di dimensione del file): il vecchio resta intero */
    ph_cfg_percorso_preferenze(percorso, sizeof percorso, &e);
    CONTROLLA(ph_cfg_leggi_file(percorso, 1 << 20, &b, &e) == 0);
    size_t vecchia = b.lunghezza;
    char *copia_vecchia = copia_esatta((char *)b.dati, b.lunghezza);
    struct rlimit vecchio, nuovo;
    getrlimit(RLIMIT_FSIZE, &vecchio);
    signal(SIGXFSZ, SIG_IGN);
    nuovo = vecchio;
    nuovo.rlim_cur = 64;
    setrlimit(RLIMIT_FSIZE, &nuovo);
    strcpy(p.cartella_ricevuti, "/una/cartella/abbastanza/lunga/da/superare/il/limite/di/sessantaquattro/byte/del/file");
    int rr = ph_cfg_preferenze_salva(&p, &e);
    setrlimit(RLIMIT_FSIZE, &vecchio);
    CONTROLLA(rr < 0 && e.testo[0] != '\0');
    CONTROLLA(ph_cfg_leggi_file(percorso, 1 << 20, &b, &e) == 0 && b.lunghezza == vecchia && memcmp(b.dati, copia_vecchia, vecchia) == 0);
    elenco(dir, elen, sizeof elen);
    CONTROLLA(strstr(elen, "|.") == NULL && elen[0] != '.'); /* nessun temporaneo abbandonato */
    free(copia_vecchia);
    /* e a limite tolto riesce */
    CONTROLLA(ph_cfg_preferenze_salva(&p, &e) == 0);

    /* cartella non scrivibile (se non si e' root) */
    if (geteuid() != 0) {
        chmod(dir, 0500);
        CONTROLLA(ph_cfg_telefono_salva("R5CT0000009", t, &e) < 0);
        CONTROLLA(ph_cfg_preferenze_salva(&p, &e) < 0);
        chmod(dir, 0700);
        elenco(dir, elen, sizeof elen);
        CONTROLLA(strstr(elen, "R5CT0000009") == NULL && strstr(elen, "|.") == NULL && elen[0] != '.');
    }

    /* due salvataggi di fila: il secondo sostituisce il primo per intero */
    t->n_icone = 1;
    CONTROLLA(ph_cfg_telefono_salva("R5CT0000000", t, &e) == 0);
    CONTROLLA(ph_cfg_telefono_carica("R5CT0000000", r, &e) == 0 && r->n_icone == 1);

    ph_buffer_libera(&b);
    free(ts);
    ph_cfg_telefono_libera(t);
    ph_cfg_telefono_libera(r);
}

/* ---------- sicurezza della cartella, temporanei, ambiente, lingua a virgola ---------- */

static void invecchia(const char *percorso, long secondi)
{
    struct timeval tv[2];
    gettimeofday(&tv[0], NULL);
    tv[0].tv_sec -= secondi;
    tv[1] = tv[0];
    utimes(percorso, tv);
}

static bool esiste(const char *p)
{
    struct stat st;
    return lstat(p, &st) == 0;
}

static void prova_cartella_e_temporanei(void)
{
    PhErrore e = {{0}};
    char dir[600], cache[600], p[800];
    imposta_ambiente("sicurezza");
    ph_cfg_cartella_config(dir, sizeof dir, &e);
    ph_cfg_cartella_cache(cache, sizeof cache, &e);

    /* cartella YASDE scrivibile da altri: si rifiuta; ripristinata, si scrive */
    snprintf(p, sizeof p, "%s/uno.toml", dir);
    CONTROLLA(ph_cfg_scrivi_file(p, "a", 1, &e) == 0);
    struct stat sd;
    chmod(dir, 0775); /* come con umask 0002: accettata e corretta */
    CONTROLLA(ph_cfg_scrivi_file(p, "b", 1, &e) == 0);
    CONTROLLA(stat(dir, &sd) == 0 && (sd.st_mode & 0777) == 0755);
    chmod(dir, 0777);
    CONTROLLA(ph_cfg_scrivi_file(p, "c", 1, &e) == 0 && stat(dir, &sd) == 0 && (sd.st_mode & 0777) == 0755);
    chmod(dir, 0700);

    /* temporanei abbandonati */
    snprintf(p, sizeof p, "%s/x", cache);
    CONTROLLA(ph_cfg_scrivi_file(p, "x", 1, &e) == 0); /* crea la cache */
    char vecchio_c[800], giovane_c[800], altro[800], corto[800], nascosto[800], vecchio_k[800], sottocart[800];
    snprintf(vecchio_c, sizeof vecchio_c, "%s/.preferenze.toml.AbC123", dir);
    snprintf(giovane_c, sizeof giovane_c, "%s/.telefoni.toml.XyZ789", dir);
    snprintf(altro, sizeof altro, "%s/vecchio.txt", dir);
    snprintf(corto, sizeof corto, "%s/.x.toml.12345", dir);
    snprintf(nascosto, sizeof nascosto, "%s/.nascosto", dir);
    snprintf(vecchio_k, sizeof vecchio_k, "%s/.sfondo.png.Q1w2E3", cache);
    snprintf(sottocart, sizeof sottocart, "%s/.dir.toml.ZZZZZZ", dir);
    const char *tutti[] = {vecchio_c, giovane_c, altro, corto, nascosto, vecchio_k};
    for (int i = 0; i < 6; i++) {
        int f = open(tutti[i], O_CREAT | O_WRONLY, 0600);
        CONTROLLA(f >= 0);
        if (f >= 0)
            close(f);
    }
    mkdir(sottocart, 0700); /* una cartella con nome da temporaneo: non si tocca */
    invecchia(vecchio_c, 7200);
    invecchia(altro, 7200);
    invecchia(corto, 7200);
    invecchia(nascosto, 7200);
    invecchia(vecchio_k, 7200);
    invecchia(sottocart, 7200);
    invecchia(giovane_c, 100);
    CONTROLLA(ph_cfg_pulisci_temporanei(3600, &e) == 2);
    CONTROLLA(!esiste(vecchio_c) && !esiste(vecchio_k));
    CONTROLLA(esiste(giovane_c) && esiste(altro) && esiste(corto) && esiste(nascosto) && esiste(sottocart));
    CONTROLLA(ph_cfg_pulisci_temporanei(3600, &e) == 0);
    CONTROLLA(ph_cfg_pulisci_temporanei(0, &e) == 1); /* sotto 60 diventa 60: il giovane (100 s) va */
    CONTROLLA(!esiste(giovane_c));
    rmdir(sottocart);
}

static void prova_senza_home(void)
{
    PhErrore e = {{0}};
    char out[800];
    unsetenv("HOME");
    unsetenv("XDG_CONFIG_HOME");
    unsetenv("XDG_CACHE_HOME");
    struct passwd pw, *res = NULL;
    char buf[2048];
    if (getpwuid_r(getuid(), &pw, buf, sizeof buf, &res) == 0 && res != NULL && pw.pw_dir != NULL && pw.pw_dir[0] == '/') {
        char atteso[900];
        snprintf(atteso, sizeof atteso, "%s/.config/YASDE", pw.pw_dir);
        CONTROLLA(ph_cfg_cartella_config(out, sizeof out, &e) == 0 && strcmp(out, atteso) == 0);
    } else {
        CONTROLLA(ph_cfg_cartella_config(out, sizeof out, &e) < 0 && e.testo[0] != '\0');
    }
    /* HOME relativo: non vale */
    setenv("HOME", "relativo", 1);
    CONTROLLA(strncmp(out, "relativo", 8) != 0);
    int r = ph_cfg_cartella_config(out, sizeof out, &e);
    CONTROLLA(r < 0 || strncmp(out, "relativo", 8) != 0);
    imposta_ambiente("iniziale");
}

static bool localizzazione_a_virgola(void)
{
    static const char *nomi[] = {"it_IT.UTF-8", "it_IT.utf8", "de_DE.UTF-8", "de_DE.utf8", "fr_FR.UTF-8", "fr_FR.utf8", "it_IT", "de_DE"};
    for (size_t i = 0; i < sizeof nomi / sizeof nomi[0]; i++)
        if (setlocale(LC_ALL, nomi[i]) != NULL) {
            struct lconv *lc = localeconv();
            if (lc->decimal_point[0] == ',')
                return true;
            setlocale(LC_ALL, "C");
        }
    /* tenta di generarne una in una cartella temporanea */
    char cmd[1024], loc[600];
    snprintf(loc, sizeof loc, "%s/locale", radice);
    snprintf(cmd, sizeof cmd, "localedef -i de_DE -f UTF-8 '%s/de_DE.UTF-8' >/dev/null 2>&1", loc);
    mkdir(loc, 0700);
    if (system(cmd) != -1) {
        setenv("LOCPATH", loc, 1);
        if (setlocale(LC_ALL, "de_DE.UTF-8") != NULL && localeconv()->decimal_point[0] == ',')
            return true;
        setlocale(LC_ALL, "C");
    }
    return false;
}

static void prova_decimali_virgola(void)
{
    if (!localizzazione_a_virgola()) {
        fprintf(stderr, "SALTATA la prova dei decimali con lingua a virgola: nessuna localizzazione disponibile nel container\n");
        return;
    }
    PhErrore e = {{0}};
    PhCfgPreferenze p, q;
    ph_cfg_preferenze_predefinite(&p);
    p.scala_automatica = false;
    p.scala = 1.25;
    PhBuffer b = PH_BUFFER_VUOTO;
    CONTROLLA(ph_cfg_preferenze_in_testo(&p, &b) == 0);
    CONTROLLA(strstr((char *)b.dati, "scala = 1.25") != NULL); /* punto, non virgola */
    CONTROLLA(leggi_pref((char *)b.dati, b.lunghezza, &q, &e) == 0 && q.scala == 1.25);
    ph_buffer_libera(&b);
    setlocale(LC_ALL, "C");
    fprintf(stderr, "prova decimali con lingua a virgola eseguita\n");
}

/* ---------- pulizia degli sfondi vecchi ---------- */

static void crea_vuoto(const char *percorso)
{
    int f = open(percorso, O_CREAT | O_WRONLY, 0600);
    if (f >= 0)
        close(f);
}

static int readdir_chiamate, readdir_guasto_a;
static struct dirent *readdir_guasto(DIR *d)
{
    readdir_chiamate++;
    if (readdir_chiamate == readdir_guasto_a) {
        errno = EIO;
        return NULL;
    }
    return readdir(d);
}

static void prova_pulisci_sfondi(void)
{
    PhErrore e = {{0}};
    char dir[600], cache[600], p[800], fuori[800], bersaglio[900];
    imposta_ambiente("sfondi");
    ph_cfg_cartella_config(dir, sizeof dir, &e);
    ph_cfg_cartella_cache(cache, sizeof cache, &e);

    /* senza cartella di configurazione non si cancella niente */
    snprintf(p, sizeof p, "%s/x", cache);
    CONTROLLA(ph_cfg_scrivi_file(p, "x", 1, &e) == 0);
    snprintf(p, sizeof p, "%s/sfondo-1.png", cache);
    crea_vuoto(p);
    invecchia(p, 7200);
    CONTROLLA(ph_cfg_pulisci_sfondi(&e) == 0 && esiste(p));

    /* profili: A nomina sfondo-1, B nomina sfondo-3, un terzo e' ostile */
    PhCfgTelefono *t = ph_cfg_telefono_nuovo();
    t->sfondo.tipo = PH_CFG_SFONDO_IMMAGINE;
    strcpy(t->sfondo.immagine, "sfondo-1.png");
    CONTROLLA(ph_cfg_telefono_salva("R5CT0000000", t, &e) == 0);
    strcpy(t->sfondo.immagine, "sfondo-3.png");
    CONTROLLA(ph_cfg_telefono_salva("R5CT0000001", t, &e) == 0);
    snprintf(fuori, sizeof fuori, "%s/sfondi/fuori", radice);
    mkdir(fuori, 0700);
    snprintf(bersaglio, sizeof bersaglio, "%s/sfondo-8.png", fuori);
    crea_vuoto(bersaglio);
    invecchia(bersaglio, 7200);
    const char *ostile = "[sfondo]\nimmagine = \"../../../fuori/sfondo-8.png\"\n";
    snprintf(p, sizeof p, "%s/telefono-OSTILE.toml", dir);
    CONTROLLA(ph_cfg_scrivi_file(p, ostile, strlen(ostile), &e) == 0);
    const char *ostile2 = "[sfondo]\nimmagine = \"sfondo-2.png/../sfondo-2.png\"\n";
    snprintf(p, sizeof p, "%s/telefono-OSTILE2.toml", dir);
    CONTROLLA(ph_cfg_scrivi_file(p, ostile2, strlen(ostile2), &e) == 0);
    /* una copia messa da parte, rovinata: non conta come profilo */
    snprintf(p, sizeof p, "%s/telefono-VECCHIO.toml.illeggibile", dir);
    CONTROLLA(ph_cfg_scrivi_file(p, "[rotto", 6, &e) == 0);

    /* file nella cache */
    const char *nomi[] = {"sfondo-1.png", "sfondo-2.png", "sfondo-3.png", "sfondo-4.png", "sfondo-5.png.bak",
                          "sfondo-x.png", "sfondo-.png", "sfondo-12345678901.png", "altro.png", "Sfondo-9.png",
                          "sfondo-10.PNG", "sfondo-0001.png"};
    for (size_t i = 0; i < sizeof nomi / sizeof nomi[0]; i++) {
        snprintf(p, sizeof p, "%s/%s", cache, nomi[i]);
        crea_vuoto(p);
        invecchia(p, i == 3 ? 120 : 7200); /* sfondo-4: recente */
    }
    char lnk[900], dirs[900], esterno[900];
    snprintf(esterno, sizeof esterno, "%s/esterno.png", fuori);
    crea_vuoto(esterno);
    invecchia(esterno, 7200);
    snprintf(lnk, sizeof lnk, "%s/sfondo-6.png", cache);
    CONTROLLA(symlink(esterno, lnk) == 0);
    snprintf(dirs, sizeof dirs, "%s/sfondo-7.png", cache);
    CONTROLLA(mkdir(dirs, 0700) == 0);
    invecchia(dirs, 7200);

    /* i due profili ostili non riferiscono nulla di valido: sfondo-2 non e' riferito da nessuno */
    int tolti = ph_cfg_pulisci_sfondi(&e);
    char q[800];
    snprintf(q, sizeof q, "%s/sfondo-1.png", cache);
    CONTROLLA(esiste(q)); /* riferito */
    snprintf(q, sizeof q, "%s/sfondo-3.png", cache);
    CONTROLLA(esiste(q)); /* riferito dall'altro profilo */
    snprintf(q, sizeof q, "%s/sfondo-2.png", cache);
    CONTROLLA(!esiste(q)); /* non riferito e vecchio: tolto */
    snprintf(q, sizeof q, "%s/sfondo-4.png", cache);
    CONTROLLA(esiste(q)); /* recente */
    snprintf(q, sizeof q, "%s/sfondo-0001.png", cache);
    CONTROLLA(!esiste(q)); /* solo cifre: e' un nome nostro */
    snprintf(q, sizeof q, "%s/sfondo-10.PNG", cache);
    CONTROLLA(esiste(q));
    for (int i = 4; i < 10; i++) { /* nomi che non corrispondono esattamente */
        if (i == 6 || i == 7)
            continue;
        snprintf(q, sizeof q, "%s/%s", cache, nomi[i]);
        CONTROLLA(esiste(q));
    }
    CONTROLLA(tolti == 2);
    CONTROLLA(esiste(bersaglio) && esiste(esterno)); /* mai fuori dalla cache */
    CONTROLLA(esiste(lnk)); /* il collegamento resta */
    struct stat st;
    CONTROLLA(stat(dirs, &st) == 0 && S_ISDIR(st.st_mode)); /* le cartelle restano */

    /* un profilo rovinato (o troppo grande) blocca tutto: -2, niente cancellato */
    snprintf(q, sizeof q, "%s/sfondo-0001.png", cache);
    crea_vuoto(q); /* di nuovo, vecchio e non riferito */
    invecchia(q, 7200);
    snprintf(p, sizeof p, "%s/telefono-ROTTO.toml", dir);
    CONTROLLA(ph_cfg_scrivi_file(p, "[sfondo\nimmagine", 16, &e) == 0);
    CONTROLLA(ph_cfg_pulisci_sfondi(&e) == -2 && esiste(q) && strstr(e.testo, "ROTTO") != NULL);
    {
        int g = open(p, O_WRONLY | O_TRUNC, 0600);
        if (g >= 0) {
            if (ftruncate(g, 3 * 1024 * 1024) == 0)
                CONTROLLA(ph_cfg_pulisci_sfondi(&e) == -2 && esiste(q));
            close(g);
        }
    }
    /* un profilo che e' una cartella (non si legge): -2 */
    unlink(p);
    CONTROLLA(mkdir(p, 0700) == 0);
    CONTROLLA(ph_cfg_pulisci_sfondi(&e) == -2 && esiste(q));
    rmdir(p);
    /* risolto: ora la pulizia riprende */
    CONTROLLA(ph_cfg_pulisci_sfondi(&e) == 1 && !esiste(q));
    CONTROLLA(ph_cfg_pulisci_sfondi(&e) == 0);

    /* errore di readdir: non e' la fine dell'elenco. Voci della cartella di
     * configurazione (con . e ..) = M, quindi il ciclo usa M+1 chiamate. */
    snprintf(q, sizeof q, "%s/sfondo-0001.png", cache);
    crea_vuoto(q);
    invecchia(q, 7200);
    int voci = 0;
    {
        DIR *d = opendir(dir);
        while (d != NULL && readdir(d) != NULL)
            voci++;
        if (d != NULL)
            closedir(d);
    }
    ph_cfg_readdir_prova = readdir_guasto;
    readdir_chiamate = 0;
    readdir_guasto_a = 1; /* sulla cartella di configurazione: nulla cancellato, cache non aperta */
    CONTROLLA(ph_cfg_pulisci_sfondi(&e) == -1 && esiste(q) && strstr(e.testo, "errore leggendo") != NULL);
    CONTROLLA(readdir_chiamate == 1);
    readdir_chiamate = 0;
    readdir_guasto_a = voci + 1 + 1; /* prima chiamata sulla cache */
    CONTROLLA(ph_cfg_pulisci_sfondi(&e) == -1 && esiste(q));
    readdir_chiamate = 0;
    readdir_guasto_a = 0; /* nessun guasto: funziona e toglie */
    CONTROLLA(ph_cfg_pulisci_sfondi(&e) == 1 && !esiste(q));
    ph_cfg_readdir_prova = NULL;

    /* oltre 1024 profili: -1 e nulla cancellato (file vuoti: profili validi, senza fsync) */
    crea_vuoto(q);
    invecchia(q, 7200);
    for (int i = 0; i < 1030; i++) {
        snprintf(p, sizeof p, "%s/telefono-p%04d.toml", dir, i);
        crea_vuoto(p);
    }
    CONTROLLA(ph_cfg_pulisci_sfondi(&e) == -1 && esiste(q) && strstr(e.testo, "troppi profili") != NULL);
    for (int i = 0; i < 1030; i++) {
        snprintf(p, sizeof p, "%s/telefono-p%04d.toml", dir, i);
        unlink(p);
    }
    CONTROLLA(ph_cfg_pulisci_sfondi(&e) == 1 && !esiste(q));
    /* NON provato: la cartella di cache che non e' dell'utente. Nel container si
     * gira da root (e non si puo' cambiare proprietario in modo portabile): il
     * ramo "st_uid != geteuid()" resta senza prova automatica. */
    ph_cfg_telefono_libera(t);
}

/* ---------- app fissate sulla barra ---------- */

static void prova_fissate(void)
{
    PhErrore e = {{0}};
    PhCfgTelefono *t = ph_cfg_telefono_nuovo(), *r = ph_cfg_telefono_nuovo();
    PhBuffer b = PH_BUFFER_VUOTO;
    CONTROLLA(t->n_fissate == 0 && !ph_cfg_telefono_app_fissata(t, "a.b"));

    /* funzioni */
    CONTROLLA(ph_cfg_telefono_fissa_app(t, "com.a", &e) == 0 && ph_cfg_telefono_fissa_app(t, "com.b", &e) == 0 &&
              ph_cfg_telefono_fissa_app(t, "com.c", &e) == 0);
    CONTROLLA(ph_cfg_telefono_fissa_app(t, "com.b", &e) == 0 && t->n_fissate == 3); /* gia' presente */
    CONTROLLA(ph_cfg_telefono_app_fissata(t, "com.b") && !ph_cfg_telefono_app_fissata(t, "com") &&
              !ph_cfg_telefono_app_fissata(t, "com.bb") && !ph_cfg_telefono_app_fissata(t, NULL));
    CONTROLLA(strcmp(t->fissate[0], "com.a") == 0 && strcmp(t->fissate[2], "com.c") == 0); /* in coda */
    CONTROLLA(ph_cfg_telefono_fissa_app(t, "", &e) < 0 && ph_cfg_telefono_fissa_app(t, NULL, &e) < 0);
    char lungo[PH_CFG_LUN_APP + 10];
    memset(lungo, 'x', sizeof lungo - 1);
    lungo[sizeof lungo - 1] = '\0';
    CONTROLLA(ph_cfg_telefono_fissa_app(t, lungo, &e) < 0 && t->n_fissate == 3);
    lungo[PH_CFG_LUN_APP - 1] = '\0'; /* 191 caratteri: entra */
    CONTROLLA(ph_cfg_telefono_fissa_app(t, lungo, &e) == 0 && t->n_fissate == 4);
    CONTROLLA(ph_cfg_telefono_togli_app(t, lungo) == 1 && t->n_fissate == 3);
    CONTROLLA(ph_cfg_telefono_togli_app(t, "com.b") == 1 && t->n_fissate == 2 && strcmp(t->fissate[0], "com.a") == 0 &&
              strcmp(t->fissate[1], "com.c") == 0 && t->fissate[2][0] == '\0'); /* ordine mantenuto */
    CONTROLLA(ph_cfg_telefono_togli_app(t, "com.b") == 0 && ph_cfg_telefono_togli_app(t, NULL) == 0);
    CONTROLLA(ph_cfg_telefono_togli_app(t, "com.a") == 1 && ph_cfg_telefono_togli_app(t, "com.c") == 1 && t->n_fissate == 0);
    for (int i = 0; i < PH_CFG_MAX_FISSATE; i++) {
        char n[32];
        snprintf(n, sizeof n, "com.app%d", i);
        CONTROLLA(ph_cfg_telefono_fissa_app(t, n, &e) == 0);
    }
    CONTROLLA(ph_cfg_telefono_fissa_app(t, "com.una.di.troppo", &e) < 0 && t->n_fissate == PH_CFG_MAX_FISSATE);
    CONTROLLA(ph_cfg_telefono_fissa_app(t, "com.app3", &e) == 0); /* presente anche a elenco pieno */

    /* andata e ritorno, con ordine */
    CONTROLLA(ph_cfg_telefono_in_testo(t, &b) == 0 && leggi_tel((char *)b.dati, b.lunghezza, r, &e) == 0 && uguali_telefono(t, r));
    CONTROLLA(r->n_fissate == 16 && strcmp(r->fissate[15], "com.app15") == 0);

    /* file vecchio senza la chiave: nessuna */
    const char *vecchio = "[audio]\nvolume = 10\n[[icona]]\napp = \"a\"\ncolonna = 0\nriga = 0\n";
    CONTROLLA(leggi_tel(vecchio, strlen(vecchio), r, &e) == 0 && r->n_fissate == 0);
    /* file ostili: non valide, doppie, tipo sbagliato, troppo lunghe; il resto si legge */
    PhBuffer l = PH_BUFFER_VUOTO;
    ph_buffer_aggiungi(&l, "[[fissata]]\napp = \"uno\"\n[[fissata]]\napp = \"\"\n[[fissata]]\napp = 5\n[[fissata]]\n"
                           "[[fissata]]\napp = \"uno\"\n[[fissata]]\napp = \"due\"\n[[fissata]]\napp = \"",
                       strlen("[[fissata]]\napp = \"uno\"\n[[fissata]]\napp = \"\"\n[[fissata]]\napp = 5\n[[fissata]]\n"
                              "[[fissata]]\napp = \"uno\"\n[[fissata]]\napp = \"due\"\n[[fissata]]\napp = \""));
    for (int i = 0; i < 500; i++)
        ph_buffer_aggiungi(&l, "z", 1);
    ph_buffer_aggiungi(&l, "\"\n[[fissata]]\napp = \"tre\"\n", strlen("\"\n[[fissata]]\napp = \"tre\"\n"));
    CONTROLLA(leggi_tel((char *)l.dati, l.lunghezza, r, &e) == 0 && r->n_fissate == 3 && strcmp(r->fissate[0], "uno") == 0 &&
              strcmp(r->fissate[1], "due") == 0 && strcmp(r->fissate[2], "tre") == 0);
    ph_buffer_svuota(&l);
    for (int i = 0; i < 100; i++) {
        char riga[64];
        int k = snprintf(riga, sizeof riga, "[[fissata]]\napp = \"p%d\"\n", i);
        ph_buffer_aggiungi(&l, riga, (size_t)k);
    }
    CONTROLLA(leggi_tel((char *)l.dati, l.lunghezza, r, &e) == 0 && r->n_fissate == PH_CFG_MAX_FISSATE &&
              strcmp(r->fissate[15], "p15") == 0); /* oltre 16 troncato */
    ph_buffer_libera(&l);
    /* 20 voci: ne restano 16, le prime nell'ordine del file */
    ph_buffer_svuota(&l);
    for (int i = 0; i < 20; i++) {
        char riga[64];
        int k = snprintf(riga, sizeof riga, "[[fissata]]\napp = \"v%02d\"\n", i);
        ph_buffer_aggiungi(&l, riga, (size_t)k);
    }
    CONTROLLA(leggi_tel((char *)l.dati, l.lunghezza, r, &e) == 0 && r->n_fissate == 16);
    for (size_t i = 0; i < r->n_fissate && i < 16; i++) {
        char atteso[8];
        snprintf(atteso, sizeof atteso, "v%02d", (int)i);
        CONTROLLA(strcmp(r->fissate[i], atteso) == 0);
    }
    CONTROLLA(!ph_cfg_telefono_app_fissata(r, "v16") && !ph_cfg_telefono_app_fissata(r, "v19"));
    /* n_fissate fuori misura (struttura riempita male): togli_app non esce dall'elenco */
    r->n_fissate = 1000;
    CONTROLLA(ph_cfg_telefono_togli_app(r, "assente") == 0 && r->n_fissate == 16);
    ph_buffer_libera(&l);
    /* file rovinato: predefiniti */
    const char *rotto = "[[fissata]]\napp = \"aperta\n";
    CONTROLLA(leggi_tel(rotto, strlen(rotto), r, &e) < 0 && r->n_fissate == 0 && e_predefinito(r));
    ph_buffer_libera(&b);
    ph_cfg_telefono_libera(t);
    ph_cfg_telefono_libera(r);
}

/* ---------- stile e opzione dei widget ---------- */

static void prova_stile_widget(void)
{
    PhErrore e = {{0}};
    PhCfgTelefono *t = ph_cfg_telefono_nuovo(), *r = ph_cfg_telefono_nuovo();
    PhBuffer b = PH_BUFFER_VUOTO;
    /* i widget predefiniti hanno stile e opzione 0 */
    for (size_t i = 0; i < t->n_widget; i++)
        CONTROLLA(t->widget[i].stile == 0 && t->widget[i].opzione == 0);
    /* scrittura e rilettura, compresi gli estremi */
    t->n_widget = 3;
    /* valori diversi da 0 e diversi tra loro, su piu' widget */
    t->widget[0].stile = 3;
    t->widget[0].opzione = 100;
    t->widget[1].stile = 15;
    t->widget[1].opzione = 255;
    t->widget[2].stile = 7;
    t->widget[2].opzione = 42;
    CONTROLLA(ph_cfg_telefono_in_testo(t, &b) == 0);
    /* il testo scritto contiene, nell'ordine, le coppie stile/opzione di ogni widget */
    {
        const char *attese[] = {"stile = 3\nopzione = 100\n", "stile = 15\nopzione = 255\n", "stile = 7\nopzione = 42\n"};
        const char *da = (const char *)b.dati;
        for (int i = 0; i < 3; i++) {
            const char *trovato = da != NULL ? strstr(da, attese[i]) : NULL;
            CONTROLLA(trovato != NULL);
            if (trovato != NULL)
                da = trovato + strlen(attese[i]);
        }
    }
    CONTROLLA(leggi_tel((char *)b.dati, b.lunghezza, r, &e) == 0 && r->n_widget == 3);
    CONTROLLA(r->widget[0].stile == 3 && r->widget[0].opzione == 100);
    CONTROLLA(r->widget[1].stile == 15 && r->widget[1].opzione == 255);
    CONTROLLA(r->widget[2].stile == 7 && r->widget[2].opzione == 42);
    /* "stile" non deve finire in "opzione" ne' viceversa */
    CONTROLLA(r->widget[0].stile != r->widget[0].opzione && r->widget[2].stile != r->widget[2].opzione);
    /* file vecchio senza le chiavi: 0 */
    const char *vecchio = "[[widget]]\ntipo = \"meteo\"\ndestra = 5\nalto = 6\ndimensione = \"grande\"\n";
    CONTROLLA(leggi_tel(vecchio, strlen(vecchio), r, &e) == 0 && r->n_widget == 1 && r->widget[0].tipo == PH_CFG_WIDGET_METEO &&
              r->widget[0].stile == 0 && r->widget[0].opzione == 0 && r->widget[0].destra == 5);
    /* valori non validi: 0, il resto della voce e il caricamento restano */
    const char *male[] = {"stile = 16\nopzione = 256\n", "stile = -1\nopzione = -1\n", "stile = \"3\"\nopzione = \"4\"\n",
                          "stile = 2.5\nopzione = true\n", "stile = 99999999999\nopzione = 3\n"};
    for (int i = 0; i < 5; i++) {
        char testo[256];
        int k = snprintf(testo, sizeof testo, "[[widget]]\ntipo = \"note\"\ndestra = 9\n%s", male[i]);
        int rr = leggi_tel(testo, (size_t)k, r, &e);
        if (i == 4) { /* 99999999999 entra in 64 bit: fuori intervallo, 0; opzione 3 valida */
            CONTROLLA(rr == 0 && r->n_widget == 1 && r->widget[0].stile == 0 && r->widget[0].opzione == 3);
        } else {
            CONTROLLA(rr == 0 && r->n_widget == 1 && r->widget[0].destra == 9 && r->widget[0].stile == 0 &&
                      r->widget[0].opzione == 0);
        }
    }
    /* intero oltre i 64 bit: errore di sintassi come per ogni altro numero */
    const char *enorme = "[[widget]]\ntipo = \"note\"\nstile = 99999999999999999999\n";
    CONTROLLA(leggi_tel(enorme, strlen(enorme), r, &e) < 0 && e_predefinito(r));
    ph_buffer_libera(&b);
    ph_cfg_telefono_libera(t);
    ph_cfg_telefono_libera(r);
}

/* ---------- misure libere dei widget ---------- */

static void prova_misure_widget(void)
{
    PhErrore e = {{0}};
    PhCfgTelefono *t = ph_cfg_telefono_nuovo(), *r = ph_cfg_telefono_nuovo();
    PhBuffer b = PH_BUFFER_VUOTO;
    /* i predefiniti hanno 0/0 e non scrivono le chiavi */
    for (size_t i = 0; i < t->n_widget; i++)
        CONTROLLA(t->widget[i].larghezza == 0 && t->widget[i].altezza == 0);
    CONTROLLA(ph_cfg_telefono_in_testo(t, &b) == 0);
    CONTROLLA(b.dati != NULL && strstr((const char *)b.dati, "larghezza") == NULL &&
              strstr((const char *)b.dati, "altezza") == NULL);
    ph_buffer_libera(&b);
    /* andata e ritorno, estremi compresi; una sola misura presente */
    t->n_widget = 3;
    t->widget[0].larghezza = 48;
    t->widget[0].altezza = 2048;
    t->widget[1].larghezza = 300;
    t->widget[2].altezza = 77;
    CONTROLLA(ph_cfg_telefono_in_testo(t, &b) == 0);
    CONTROLLA(b.dati != NULL && strstr((const char *)b.dati, "larghezza = 300\n") != NULL &&
              strstr((const char *)b.dati, "altezza = 77\n") != NULL && strstr((const char *)b.dati, "dimensione") != NULL);
    CONTROLLA(leggi_tel((char *)b.dati, b.lunghezza, r, &e) == 0 && r->n_widget == 3);
    CONTROLLA(r->widget[0].larghezza == 48 && r->widget[0].altezza == 2048);
    CONTROLLA(r->widget[1].larghezza == 300 && r->widget[1].altezza == 0);
    CONTROLLA(r->widget[2].larghezza == 0 && r->widget[2].altezza == 77);
    ph_buffer_libera(&b);
    /* file vecchio senza le chiavi: 0 */
    const char *vecchio = "[[widget]]\ntipo = \"meteo\"\ndestra = 5\nalto = 6\ndimensione = \"grande\"\n";
    CONTROLLA(leggi_tel(vecchio, strlen(vecchio), r, &e) == 0 && r->n_widget == 1 && r->widget[0].larghezza == 0 &&
              r->widget[0].altezza == 0 && r->widget[0].dimensione == PH_CFG_WIDGET_GRANDE);
    /* valori non validi: 0, il resto della voce e il caricamento restano; l'altra misura valida resta */
    const char *male[] = {"larghezza = 47\naltezza = 100\n", "larghezza = 2049\naltezza = 100\n",
                          "larghezza = -1\naltezza = 100\n", "larghezza = \"300\"\naltezza = 100\n",
                          "larghezza = 300.5\naltezza = 100\n", "larghezza = 99999999999\naltezza = 100\n",
                          "larghezza = 1\naltezza = 100\n"};
    for (int i = 0; i < 7; i++) {
        char testo[256];
        int k = snprintf(testo, sizeof testo, "[[widget]]\ntipo = \"note\"\ndestra = 9\n%s", male[i]);
        CONTROLLA(leggi_tel(testo, (size_t)k, r, &e) == 0 && r->n_widget == 1 && r->widget[0].destra == 9 &&
                  r->widget[0].larghezza == 0 && r->widget[0].altezza == 100);
    }
    /* altezza non valida con larghezza valida (ramo speculare) */
    const char *alt = "[[widget]]\ntipo = \"note\"\nlarghezza = 200\naltezza = 3000\n";
    CONTROLLA(leggi_tel(alt, strlen(alt), r, &e) == 0 && r->widget[0].larghezza == 200 && r->widget[0].altezza == 0);
    const char *zero = "[[widget]]\ntipo = \"note\"\nlarghezza = 0\naltezza = 0\n";
    CONTROLLA(leggi_tel(zero, strlen(zero), r, &e) == 0 && r->widget[0].larghezza == 0 && r->widget[0].altezza == 0);
    ph_cfg_telefono_libera(t);
    ph_cfg_telefono_libera(r);
}

/* ---------- widget nuovi e conteggio delle aperture ---------- */

static void prova_aperture(void)
{
    PhErrore e = {{0}};
    PhCfgTelefono *t = ph_cfg_telefono_nuovo(), *r = ph_cfg_telefono_nuovo();
    PhBuffer b = PH_BUFFER_VUOTO;

    /* i valori dei tipi esistenti non cambiano; i nuovi sono in coda */
    CONTROLLA(PH_CFG_WIDGET_OROLOGIO == 0 && PH_CFG_WIDGET_RIPRODUZIONE == 7 && PH_CFG_WIDGET_CALENDARIO_MESE == 8 &&
              PH_CFG_WIDGET_TRASFERIMENTI == 14);
    {
        const char *nomi[] = {"calendario-mese", "sveglia", "batteria", "interruttori", "trova", "app-frequenti", "trasferimenti"};
        PhBuffer l = PH_BUFFER_VUOTO;
        for (int i = 0; i < 7; i++) {
            ph_buffer_aggiungi(&l, "[[widget]]\ntipo = \"", 19);
            ph_buffer_aggiungi(&l, nomi[i], strlen(nomi[i]));
            ph_buffer_aggiungi(&l, "\"\n", 2);
        }
        ph_buffer_aggiungi(&l, "[[widget]]\ntipo = \"inesistente\"\n", strlen("[[widget]]\ntipo = \"inesistente\"\n"));
        CONTROLLA(leggi_tel((char *)l.dati, l.lunghezza, r, &e) == 0 && r->n_widget == 7);
        for (int i = 0; i < 7 && r->n_widget == 7; i++)
            CONTROLLA((int)r->widget[i].tipo == (int)PH_CFG_WIDGET_CALENDARIO_MESE + i);
        ph_buffer_libera(&l);
    }

    /* conteggio */
    t->n_aperture = 0;
    CONTROLLA(ph_cfg_telefono_conta_apertura(t, "com.a", 1000, &e) == 0 && t->n_aperture == 1 && t->aperture[0].volte == 1 &&
              t->aperture[0].ultima == 1000);
    CONTROLLA(ph_cfg_telefono_conta_apertura(t, "com.a", 2000, &e) == 0 && t->n_aperture == 1 && t->aperture[0].volte == 2 &&
              t->aperture[0].ultima == 2000);
    CONTROLLA(ph_cfg_telefono_conta_apertura(t, "com.b", -5, &e) == 0 && t->n_aperture == 2 && t->aperture[1].ultima == 0);
    CONTROLLA(ph_cfg_telefono_conta_apertura(t, "com.b", (int64_t)1 << 40, &e) == 0 && t->aperture[1].volte == 2 &&
              t->aperture[1].ultima == 0);
    CONTROLLA(ph_cfg_telefono_conta_apertura(t, "", 1, &e) < 0 && ph_cfg_telefono_conta_apertura(t, NULL, 1, &e) < 0);
    char lungo[PH_CFG_LUN_APP + 5];
    memset(lungo, 'x', sizeof lungo - 1);
    lungo[sizeof lungo - 1] = '\0';
    CONTROLLA(ph_cfg_telefono_conta_apertura(t, lungo, 1, &e) < 0 && t->n_aperture == 2);
    /* il contatore satura */
    t->aperture[0].volte = PH_CFG_MAX_VOLTE;
    CONTROLLA(ph_cfg_telefono_conta_apertura(t, "com.a", 3000, &e) == 0 && t->aperture[0].volte == PH_CFG_MAX_VOLTE);
    /* struttura riempita male: n_aperture fuori misura si riporta a 64 */
    t->n_aperture = 100000;
    CONTROLLA(ph_cfg_telefono_conta_apertura(t, "com.assente.x", 1, &e) == 0 && t->n_aperture == PH_CFG_MAX_APERTURE);

    /* elenco pieno: via la voce con meno aperture e, a parita', la piu' vecchia */
    memset(t->aperture, 0, sizeof t->aperture);
    t->n_aperture = PH_CFG_MAX_APERTURE;
    for (int i = 0; i < PH_CFG_MAX_APERTURE; i++) {
        snprintf(t->aperture[i].app, sizeof t->aperture[i].app, "com.app%02d", i);
        t->aperture[i].volte = 2;
        t->aperture[i].ultima = 10000 + i;
    }
    t->aperture[10].volte = 1;
    t->aperture[10].ultima = 500;
    t->aperture[20].volte = 1;
    t->aperture[20].ultima = 100; /* la piu' vecchia tra le meno usate */
    t->aperture[30].volte = 1;
    t->aperture[30].ultima = 100; /* pari a 20: resta la prima trovata sotto */
    CONTROLLA(ph_cfg_telefono_conta_apertura(t, "com.nuova", 9000, &e) == 0 && t->n_aperture == PH_CFG_MAX_APERTURE);
    {
        bool c20 = false, c10 = false, c30 = false, nuova = false;
        for (size_t i = 0; i < t->n_aperture; i++) {
            c20 |= strcmp(t->aperture[i].app, "com.app20") == 0;
            c10 |= strcmp(t->aperture[i].app, "com.app10") == 0;
            c30 |= strcmp(t->aperture[i].app, "com.app30") == 0;
            if (strcmp(t->aperture[i].app, "com.nuova") == 0)
                nuova = t->aperture[i].volte == 1 && t->aperture[i].ultima == 9000;
        }
        CONTROLLA(!c20 && c10 && c30 && nuova);
    }
    CONTROLLA(ph_cfg_telefono_conta_apertura(t, "com.nuova2", 9500, &e) == 0); /* ora via app30 (ultima 100) */
    CONTROLLA(ph_cfg_telefono_conta_apertura(t, "com.nuova3", 9600, &e) == 0); /* poi app10 (500) */
    {
        bool c10 = false, c30 = false;
        for (size_t i = 0; i < t->n_aperture; i++) {
            c10 |= strcmp(t->aperture[i].app, "com.app10") == 0;
            c30 |= strcmp(t->aperture[i].app, "com.app30") == 0;
        }
        CONTROLLA(!c10 && !c30 && t->n_aperture == PH_CFG_MAX_APERTURE);
    }
    /* un'app gia' presente a elenco pieno non fa togliere nessuno */
    size_t prima = t->n_aperture;
    CONTROLLA(ph_cfg_telefono_conta_apertura(t, "com.app05", 9700, &e) == 0 && t->n_aperture == prima &&
              t->aperture[5].volte == 3);

    /* andata e ritorno e testo */
    ph_cfg_telefono_predefinito(t);
    t->aperture[0] = (PhCfgApertura){.app = "com.uno", .volte = 7, .ultima = 1700000001};
    t->aperture[1] = (PhCfgApertura){.app = "com.due", .volte = PH_CFG_MAX_VOLTE, .ultima = 4102444800LL};
    t->n_aperture = 2;
    CONTROLLA(ph_cfg_telefono_in_testo(t, &b) == 0 && strstr((char *)b.dati, "volte = 7\nultima = 1700000001\n") != NULL);
    CONTROLLA(leggi_tel((char *)b.dati, b.lunghezza, r, &e) == 0 && uguali_telefono(t, r) && r->aperture[1].volte == PH_CFG_MAX_VOLTE);

    /* file vecchio: vuoto; valori assurdi scartati senza far fallire il caricamento */
    const char *vecchio = "[audio]\nvolume = 10\n";
    CONTROLLA(leggi_tel(vecchio, strlen(vecchio), r, &e) == 0 && r->n_aperture == 0);
    const char *male =
        "[[apertura]]\napp = \"ok1\"\nvolte = 3\nultima = -5\n"     /* ultima assurda: 0 */
        "[[apertura]]\napp = \"zero\"\nvolte = 0\n"                  /* volte fuori misura */
        "[[apertura]]\napp = \"neg\"\nvolte = -4\n"
        "[[apertura]]\napp = \"enorme\"\nvolte = 2000000000\n"
        "[[apertura]]\napp = \"stringa\"\nvolte = \"5\"\n"
        "[[apertura]]\napp = \"decimale\"\nvolte = 2.5\n"
        "[[apertura]]\napp = \"\"\nvolte = 1\n"
        "[[apertura]]\nvolte = 1\n"
        "[[apertura]]\napp = \"senza-volte\"\n"
        "[[apertura]]\napp = \"ok1\"\nvolte = 9\n"                   /* doppia: resta la prima */
        "[[apertura]]\napp = \"ok2\"\nvolte = 1000000000\nultima = 99999999999999\n";
    CONTROLLA(leggi_tel(male, strlen(male), r, &e) == 0 && r->n_aperture == 2);
    if (r->n_aperture == 2) {
        CONTROLLA(strcmp(r->aperture[0].app, "ok1") == 0 && r->aperture[0].volte == 3 && r->aperture[0].ultima == 0);
        CONTROLLA(strcmp(r->aperture[1].app, "ok2") == 0 && r->aperture[1].volte == PH_CFG_MAX_VOLTE && r->aperture[1].ultima == 0);
    }
    /* oltre 64 voci: troncato, le prime restano */
    {
        PhBuffer l = PH_BUFFER_VUOTO;
        char riga[96];
        for (int i = 0; i < 100; i++) {
            int k = snprintf(riga, sizeof riga, "[[apertura]]\napp = \"p%03d\"\nvolte = %d\n", i, i + 1);
            ph_buffer_aggiungi(&l, riga, (size_t)k);
        }
        CONTROLLA(leggi_tel((char *)l.dati, l.lunghezza, r, &e) == 0 && r->n_aperture == PH_CFG_MAX_APERTURE &&
                  strcmp(r->aperture[63].app, "p063") == 0 && r->aperture[63].volte == 64);
        ph_buffer_libera(&l);
    }
    /* app troppo lunga scartata */
    {
        PhBuffer l = PH_BUFFER_VUOTO;
        ph_buffer_aggiungi(&l, "[[apertura]]\napp = \"", strlen("[[apertura]]\napp = \""));
        for (int i = 0; i < 300; i++)
            ph_buffer_aggiungi(&l, "y", 1);
        ph_buffer_aggiungi(&l, "\"\nvolte = 3\n", strlen("\"\nvolte = 3\n"));
        CONTROLLA(leggi_tel((char *)l.dati, l.lunghezza, r, &e) == 0 && r->n_aperture == 0);
        ph_buffer_libera(&l);
    }
    ph_buffer_libera(&b);
    ph_cfg_telefono_libera(t);
    ph_cfg_telefono_libera(r);
}

/* ---------- citta' del meteo ---------- */

static void prova_meteo_luogo(void)
{
    PhErrore e = {{0}};
    PhCfgTelefono *t = ph_cfg_telefono_nuovo(), *r = ph_cfg_telefono_nuovo();
    PhBuffer b = PH_BUFFER_VUOTO;
    /* predefinito: non scelta */
    CONTROLLA(!t->meteo_luogo_scelto && t->meteo_luogo_nome[0] == '\0' && t->meteo_luogo_dettaglio[0] == '\0' &&
              t->meteo_luogo_lat_centesimi == 0 && t->meteo_luogo_lon_centesimi == 0);
    CONTROLLA(ph_cfg_telefono_in_testo(t, &b) == 0 && strstr((char *)b.dati, "[meteo]") == NULL); /* non scelta: niente sezione */
    /* scelta: andata e ritorno, con UTF-8 e valori negativi/estremi */
    t->meteo_luogo_scelto = true;
    strcpy(t->meteo_luogo_nome, "Citt\xC3\xA0-Finta");
    strcpy(t->meteo_luogo_dettaglio, "Regione \"Finta\", Paese");
    t->meteo_luogo_lat_centesimi = -9000;
    t->meteo_luogo_lon_centesimi = 18000;
    ph_buffer_svuota(&b);
    CONTROLLA(ph_cfg_telefono_in_testo(t, &b) == 0 && strstr((char *)b.dati, "[meteo]") != NULL &&
              strstr((char *)b.dati, "lat_centesimi = -9000\nlon_centesimi = 18000\n") != NULL);
    CONTROLLA(leggi_tel((char *)b.dati, b.lunghezza, r, &e) == 0 && uguali_telefono(t, r) && r->meteo_luogo_scelto);
    CONTROLLA(r->meteo_luogo_lat_centesimi == -9000 && r->meteo_luogo_lon_centesimi == 18000);
    /* non scelta ma con dati dentro (residui): la sezione non si scrive */
    t->meteo_luogo_scelto = false;
    ph_buffer_svuota(&b);
    CONTROLLA(ph_cfg_telefono_in_testo(t, &b) == 0 && strstr((char *)b.dati, "[meteo]") == NULL &&
              strstr((char *)b.dati, "Finta") == NULL);
    t->meteo_luogo_scelto = true;
    /* struttura non valida (non si scrive): nome vuoto o fuori limite */
    t->meteo_luogo_nome[0] = '\0';
    ph_buffer_svuota(&b);
    CONTROLLA(ph_cfg_telefono_in_testo(t, &b) == 0 && strstr((char *)b.dati, "[meteo]") == NULL);
    strcpy(t->meteo_luogo_nome, "X");
    t->meteo_luogo_lat_centesimi = 9001;
    ph_buffer_svuota(&b);
    CONTROLLA(ph_cfg_telefono_in_testo(t, &b) == 0 && strstr((char *)b.dati, "[meteo]") == NULL);
    /* sezione assente e chiavi sconosciute (meteo_consenso): non scelta, le chiavi sono ignorate */
    const char *vecchio = "[generale]\nmeteo_consenso = true\n[audio]\nvolume = 10\n[meteo_consenso]\nx = 1\n";
    CONTROLLA(leggi_tel(vecchio, strlen(vecchio), r, &e) == 0 && !r->meteo_luogo_scelto && r->volume == 10);
    const char *vecchio2 = "[meteo]\nconsenso = true\nnome = \"Citta\"\nlat_centesimi = 1\nlon_centesimi = 2\nmeteo_consenso = true\n";
    CONTROLLA(leggi_tel(vecchio2, strlen(vecchio2), r, &e) == 0 && r->meteo_luogo_scelto && strcmp(r->meteo_luogo_nome, "Citta") == 0 &&
              r->meteo_luogo_dettaglio[0] == '\0');
    /* ogni ramo "non scelta" */
    const char *male[] = {
        "[meteo]\nnome = \"\"\nlat_centesimi = 1\nlon_centesimi = 2\n",               /* nome vuoto */
        "[meteo]\nlat_centesimi = 1\nlon_centesimi = 2\n",                              /* senza nome */
        "[meteo]\nnome = 5\nlat_centesimi = 1\nlon_centesimi = 2\n",                   /* nome non stringa */
        "[meteo]\nnome = \"C\"\nlat_centesimi = 9001\nlon_centesimi = 2\n",
        "[meteo]\nnome = \"C\"\nlat_centesimi = -9001\nlon_centesimi = 2\n",
        "[meteo]\nnome = \"C\"\nlat_centesimi = 1\nlon_centesimi = 18001\n",
        "[meteo]\nnome = \"C\"\nlat_centesimi = 1\nlon_centesimi = -18001\n",
        "[meteo]\nnome = \"C\"\nlon_centesimi = 2\n",                                    /* senza latitudine */
        "[meteo]\nnome = \"C\"\nlat_centesimi = 1\n",                                    /* senza longitudine */
        "[meteo]\nnome = \"C\"\nlat_centesimi = 1.5\nlon_centesimi = 2\n",            /* decimale */
        "[meteo]\nnome = \"C\"\nlat_centesimi = \"1\"\nlon_centesimi = 2\n",          /* stringa */
        "[meteo]\nnome = \"C\"\nlat_centesimi = 99999999999\nlon_centesimi = 2\n",
        "[meteo]\nnome = \"C\"\ndettaglio = 7\nlat_centesimi = 1\nlon_centesimi = 2\n" /* dettaglio non stringa: ignorato, vedi sotto */};
    for (int i = 0; i < 13; i++) {
        int rr = leggi_tel(male[i], strlen(male[i]), r, &e);
        CONTROLLA(rr == 0);
        if (i == 12) /* un dettaglio non stringa e' come assente: la citta' vale */
            CONTROLLA(r->meteo_luogo_scelto && r->meteo_luogo_dettaglio[0] == '\0');
        else
            CONTROLLA(!r->meteo_luogo_scelto && r->meteo_luogo_nome[0] == '\0' && r->meteo_luogo_lat_centesimi == 0 &&
                      r->meteo_luogo_lon_centesimi == 0);
    }
    /* stringhe troppo lunghe: non scelta (nome o dettaglio) */
    {
        PhBuffer l = PH_BUFFER_VUOTO;
        ph_buffer_aggiungi(&l, "[meteo]\nnome = \"", strlen("[meteo]\nnome = \""));
        for (int i = 0; i < 200; i++)
            ph_buffer_aggiungi(&l, "n", 1);
        ph_buffer_aggiungi(&l, "\"\nlat_centesimi = 1\nlon_centesimi = 2\n", strlen("\"\nlat_centesimi = 1\nlon_centesimi = 2\n"));
        CONTROLLA(leggi_tel((char *)l.dati, l.lunghezza, r, &e) == 0 && !r->meteo_luogo_scelto);
        ph_buffer_svuota(&l);
        ph_buffer_aggiungi(&l, "[meteo]\nnome = \"C\"\ndettaglio = \"", strlen("[meteo]\nnome = \"C\"\ndettaglio = \""));
        for (int i = 0; i < 128; i++) /* 128 byte: non entra (serve il terminatore) */
            ph_buffer_aggiungi(&l, "d", 1);
        ph_buffer_aggiungi(&l, "\"\nlat_centesimi = 1\nlon_centesimi = 2\n", strlen("\"\nlat_centesimi = 1\nlon_centesimi = 2\n"));
        CONTROLLA(leggi_tel((char *)l.dati, l.lunghezza, r, &e) == 0 && !r->meteo_luogo_scelto);
        ph_buffer_svuota(&l);
        ph_buffer_aggiungi(&l, "[meteo]\nnome = \"", strlen("[meteo]\nnome = \""));
        for (int i = 0; i < 127; i++) /* 127 byte: entra */
            ph_buffer_aggiungi(&l, "n", 1);
        ph_buffer_aggiungi(&l, "\"\nlat_centesimi = 1\nlon_centesimi = 2\n", strlen("\"\nlat_centesimi = 1\nlon_centesimi = 2\n"));
        CONTROLLA(leggi_tel((char *)l.dati, l.lunghezza, r, &e) == 0 && r->meteo_luogo_scelto && strlen(r->meteo_luogo_nome) == 127);
        ph_buffer_libera(&l);
    }
    /* UTF-8 non valido nel file: errore di sintassi di tutto il file; il messaggio non contiene il luogo */
    const char *rotto = "[meteo]\nnome = \"Citta-Segreta\xff\"\nlat_centesimi = 1\nlon_centesimi = 2\n";
    CONTROLLA(leggi_tel(rotto, strlen(rotto), r, &e) < 0 && strstr(e.testo, "Segreta") == NULL && e_predefinito(r));
    /* nome scritto con un byte non valido nella struttura: il file resta leggibile */
    ph_cfg_telefono_predefinito(t);
    t->meteo_luogo_scelto = true;
    strcpy(t->meteo_luogo_nome, "Bad\xff");
    t->meteo_luogo_lat_centesimi = 5;
    ph_buffer_svuota(&b);
    CONTROLLA(ph_cfg_telefono_in_testo(t, &b) == 0 && leggi_tel((char *)b.dati, b.lunghezza, r, &e) == 0 && r->meteo_luogo_scelto &&
              strcmp(r->meteo_luogo_nome, "Bad?") == 0);
    /* su disco, e il messaggio di un file rovinato non nomina il luogo */
    imposta_ambiente("meteo");
    ph_cfg_telefono_predefinito(t);
    t->meteo_luogo_scelto = true;
    strcpy(t->meteo_luogo_nome, "Citta-Segreta");
    CONTROLLA(ph_cfg_telefono_salva("R5CT0000000", t, &e) == 0 && ph_cfg_telefono_carica("R5CT0000000", r, &e) == 0 &&
              uguali_telefono(t, r));
    char pf[800];
    ph_cfg_percorso_telefono("R5CT0000000", pf, sizeof pf, &e);
    const char *rotto2 = "[meteo]\nnome = \"Citta-Segreta\nlat_centesimi = 1\n";
    CONTROLLA(ph_cfg_scrivi_file(pf, rotto2, strlen(rotto2), &e) == 0);
    CONTROLLA(ph_cfg_telefono_carica("R5CT0000000", r, &e) == 1 && strstr(e.testo, "Segreta") == NULL && !r->meteo_luogo_scelto);
    ph_buffer_libera(&b);
    ph_cfg_telefono_libera(t);
    ph_cfg_telefono_libera(r);
}

/* ---------- note rapide ---------- */

/* File con [note] testo = "<corpo>" (corpo gia' scritto in TOML). */
static int leggi_note_toml(const char *corpo, PhCfgTelefono *r, PhErrore *e)
{
    PhBuffer l = PH_BUFFER_VUOTO;
    int rr = -2;
    if (ph_buffer_aggiungi(&l, "[note]\ntesto = \"", 16) == 0 && ph_buffer_aggiungi(&l, corpo, strlen(corpo)) == 0 &&
        ph_buffer_aggiungi(&l, "\"\n", 2) == 0)
        rr = leggi_tel((char *)l.dati, l.lunghezza, r, e);
    ph_buffer_libera(&l);
    return rr;
}

/* Scrive t, rilegge in r e torna 1 se r e' identico a t. */
static int note_andata_ritorno(PhCfgTelefono *t, PhCfgTelefono *r, PhBuffer *b, PhErrore *e)
{
    ph_buffer_svuota(b);
    return ph_cfg_telefono_in_testo(t, b) == 0 && leggi_tel((char *)b->dati, b->lunghezza, r, e) == 0 && uguali_telefono(t, r) &&
           strcmp(t->note, r->note) == 0;
}

static void prova_note(void)
{
    PhErrore e = {{0}};
    PhCfgTelefono *t = ph_cfg_telefono_nuovo(), *r = ph_cfg_telefono_nuovo();
    PhBuffer b = PH_BUFFER_VUOTO;
    /* predefinito: vuoto, e non si scrive nessuna tabella */
    CONTROLLA(t->note[0] == '\0');
    CONTROLLA(ph_cfg_telefono_in_testo(t, &b) == 0 && strstr((char *)b.dati, "[note]") == NULL);
    CONTROLLA(note_andata_ritorno(t, r, &b, &e) && r->note[0] == '\0');
    /* andata e ritorno: piu' righe, virgolette, barre, tab, UTF-8 a 2, 3 e 4 byte */
    const char *testi[] = {
        "una riga",
        "uno\ndue\n\ntre\n",
        "dice \"ciao\" e \\ barra \\\\ doppia \\n non-a-capo",
        "\ttab\tdentro\t",
        "\xC3\xA8 \xE2\x82\xAC \xF0\x9F\x98\x80 fine",
        "\n",
    };
    for (size_t i = 0; i < sizeof testi / sizeof *testi; i++) {
        strcpy(t->note, testi[i]);
        CONTROLLA(note_andata_ritorno(t, r, &b, &e));
        CONTROLLA(strstr((char *)b.dati, "[note]\ntesto = \"") != NULL);
    }
    /* il testo scritto ha gli escape, non a capo veri dentro la stringa */
    strcpy(t->note, "a\nb\tc\"d\\e");
    ph_buffer_svuota(&b);
    CONTROLLA(ph_cfg_telefono_in_testo(t, &b) == 0 && strstr((char *)b.dati, "testo = \"a\\nb\\tc\\\"d\\\\e\"\n") != NULL);
    /* vuota: la chiave non si scrive, anche se c'e' roba oltre il terminatore */
    t->note[0] = '\0';
    t->note[1] = 'x';
    ph_buffer_svuota(&b);
    CONTROLLA(ph_cfg_telefono_in_testo(t, &b) == 0 && strstr((char *)b.dati, "[note]") == NULL && strstr((char *)b.dati, "testo") == NULL);
    /* campo senza terminatore (non dovrebbe esistere): non si scrive e non si legge fuori */
    memset(t->note, 'z', sizeof t->note);
    ph_buffer_svuota(&b);
    CONTROLLA(ph_cfg_telefono_in_testo(t, &b) == 0 && strstr((char *)b.dati, "[note]") == NULL);
    /* profilo vecchio senza la tabella: vuoto */
    const char *vecchio = "[audio]\nvolume = 10\n";
    strcpy(r->note, "residuo");
    CONTROLLA(leggi_tel(vecchio, strlen(vecchio), r, &e) == 0 && r->note[0] == '\0' && r->volume == 10);
    /* tabella senza la chiave, o chiave di altro tipo: vuoto, il resto del file si legge */
    const char *tipi[] = {"[note]\ntesto = 5\n[audio]\nvolume = 10\n", "[note]\ntesto = true\n[audio]\nvolume = 10\n",
                          "[note]\naltro = \"x\"\n[audio]\nvolume = 10\n", "[note]\n[audio]\nvolume = 10\n"};
    for (size_t i = 0; i < sizeof tipi / sizeof *tipi; i++) {
        strcpy(r->note, "residuo");
        CONTROLLA(leggi_tel(tipi[i], strlen(tipi[i]), r, &e) == 0 && r->note[0] == '\0' && r->volume == 10);
    }
    /* \r\n -> \n, \r isolato tolto, \r scritto come escape tolto */
    CONTROLLA(leggi_note_toml("uno\\r\\ndue\\r\\n", r, &e) == 0 && strcmp(r->note, "uno\ndue\n") == 0);
    CONTROLLA(leggi_note_toml("a\\rb\\r", r, &e) == 0 && strcmp(r->note, "ab") == 0);
    CONTROLLA(leggi_note_toml("\\r\\r", r, &e) == 0 && r->note[0] == '\0');
    /* stesso testo scritto da noi con un '\r' dentro: si rilegge senza */
    strcpy(t->note, "x\r\ny");
    ph_buffer_svuota(&b);
    CONTROLLA(ph_cfg_telefono_in_testo(t, &b) == 0 && leggi_tel((char *)b.dati, b.lunghezza, r, &e) == 0 && strcmp(r->note, "x\ny") == 0);
    /* caratteri di controllo non ammessi (anche come \u): testo vuoto, il file si legge */
    const char *ctrl[] = {"a\\u0001b", "a\\u001Fb", "a\\u007Fb", "a\\bb", "a\\fb", "\\u0007", "ok\\u0000"};
    for (size_t i = 0; i < sizeof ctrl / sizeof *ctrl; i++) {
        strcpy(r->note, "residuo");
        int rr = leggi_note_toml(ctrl[i], r, &e);
        if (i == 6) { /* \u0000 lo rifiuta il lettore: errore di sintassi, tutto predefinito */
            CONTROLLA(rr < 0 && e_predefinito(r) && r->note[0] == '\0');
        } else {
            CONTROLLA(rr == 0 && r->note[0] == '\0');
        }
    }
    /* stesso caso scritto da noi: un carattere di controllo nel campo si rilegge come testo vuoto */
    strcpy(t->note, "a\x01" "b");
    ph_buffer_svuota(&b);
    CONTROLLA(ph_cfg_telefono_in_testo(t, &b) == 0 && leggi_tel((char *)b.dati, b.lunghezza, r, &e) == 0 && r->note[0] == '\0');
    /* UTF-8 non valido o controllo in chiaro nel file: errore del file intero (lettore TOML),
     * tutto predefinito, e il messaggio non riporta il testo */
    const char *rotti[] = {"[note]\ntesto = \"Segreto\xff\"\n[audio]\nvolume = 10\n",
                           "[note]\ntesto = \"Segreto\x01\"\n[audio]\nvolume = 10\n",
                           "[note]\ntesto = \"Segreto\xC3\"\n", "[note]\ntesto = \"Segreto\n"};
    for (size_t i = 0; i < sizeof rotti / sizeof *rotti; i++) {
        strcpy(r->note, "residuo");
        CONTROLLA(leggi_tel(rotti[i], strlen(rotti[i]), r, &e) < 0 && strstr(e.testo, "Segreto") == NULL && e_predefinito(r) &&
                  r->note[0] == '\0');
    }
    /* limite: 2047 byte entrano interi, 2048 e oltre si troncano a 2047 */
    char *g = malloc(PH_CFG_LUN_NOTE + 16);
    CONTROLLA(g != NULL);
    if (g != NULL) {
        memset(g, 'n', PH_CFG_LUN_NOTE + 8);
        g[PH_CFG_LUN_NOTE - 1] = '\0'; /* 2047 */
        CONTROLLA(leggi_note_toml(g, r, &e) == 0 && strlen(r->note) == PH_CFG_LUN_NOTE - 1 && strcmp(r->note, g) == 0);
        strcpy(t->note, g);
        CONTROLLA(note_andata_ritorno(t, r, &b, &e));
        g[PH_CFG_LUN_NOTE - 1] = 'n'; /* 2048 */
        g[PH_CFG_LUN_NOTE] = '\0';
        CONTROLLA(leggi_note_toml(g, r, &e) == 0 && strlen(r->note) == PH_CFG_LUN_NOTE - 1 && strncmp(r->note, g, PH_CFG_LUN_NOTE - 1) == 0);
        g[PH_CFG_LUN_NOTE] = 'n'; /* molto oltre */
        g[PH_CFG_LUN_NOTE + 8] = '\0';
        CONTROLLA(leggi_note_toml(g, r, &e) == 0 && strlen(r->note) == PH_CFG_LUN_NOTE - 1);
        /* niente scritto oltre il campo (il successivo e' compatibilita[0]) */
        CONTROLLA(r->compatibilita[0][0] == '\0' && r->compatibilita[0][1] == '\0' && r->n_compatibilita == 0);
        /* taglio dentro un carattere a piu' byte: si torna al confine. Il carattere
         * comincia al byte 2046 (2 byte: 2046-2047), 2045 (3 byte), 2044 (4 byte). */
        const char *multi[] = {"\xC3\xA8", "\xE2\x82\xAC", "\xF0\x9F\x98\x80"};
        for (size_t m = 0; m < 3; m++) {
            size_t lm = strlen(multi[m]);
            for (size_t inizio = PH_CFG_LUN_NOTE - 1 - lm; inizio + lm <= PH_CFG_LUN_NOTE - 1 + lm; inizio++) {
                /* carattere multi[m] a partire da `inizio`, poi altre 'n'; il resto 'n' */
                memset(g, 'n', PH_CFG_LUN_NOTE + 8);
                memcpy(g + inizio, multi[m], lm);
                g[PH_CFG_LUN_NOTE + 8] = '\0';
                CONTROLLA(leggi_note_toml(g, r, &e) == 0);
                size_t n = strlen(r->note);
                CONTROLLA(n <= PH_CFG_LUN_NOTE - 1);
                if (inizio + lm <= PH_CFG_LUN_NOTE - 1) {
                    CONTROLLA(n == PH_CFG_LUN_NOTE - 1); /* il carattere entra intero */
                } else {
                    CONTROLLA(n == inizio); /* non entra: si taglia prima */
                }
                /* nessun carattere spezzato: l'ultimo byte non e' un lead rimasto solo */
                CONTROLLA(n == 0 || ((unsigned char)r->note[n - 1] & 0xC0) != 0xC0);
                /* e riscritto si rilegge identico */
                strcpy(t->note, r->note);
                CONTROLLA(note_andata_ritorno(t, r, &b, &e));
            }
        }
        /* i \r tolti non contano nel limite: 2047 'n' con dei \r in mezzo entrano interi */
        memset(g, 'n', PH_CFG_LUN_NOTE - 1);
        g[PH_CFG_LUN_NOTE - 1] = '\0';
        PhBuffer cr = PH_BUFFER_VUOTO;
        int ok_cr = 0;
        for (size_t i = 0; i < PH_CFG_LUN_NOTE - 1; i++) {
            ok_cr |= ph_buffer_aggiungi(&cr, "n", 1);
            if (i % 100 == 0)
                ok_cr |= ph_buffer_aggiungi(&cr, "\\r", 2);
        }
        ok_cr |= ph_buffer_aggiungi(&cr, "", 1);
        CONTROLLA(ok_cr == 0 && leggi_note_toml((char *)cr.dati, r, &e) == 0 && strlen(r->note) == PH_CFG_LUN_NOTE - 1 && strcmp(r->note, g) == 0);
        ph_buffer_libera(&cr);
        free(g);
    }
    /* 2047 caratteri di controllo: non si scrive la tabella, il profilo si rilegge intero
     * (volume conservato), nota vuota. Vale per ogni controllo vietato, 0x7F compreso. */
    const unsigned char vietati[] = {0x01, 0x08, 0x1F, 0x7F};
    for (size_t i = 0; i < sizeof vietati; i++) {
        ph_cfg_telefono_predefinito(t);
        t->volume = 37;
        memset(t->note, vietati[i], PH_CFG_LUN_NOTE - 1);
        t->note[PH_CFG_LUN_NOTE - 1] = '\0';
        ph_buffer_svuota(&b);
        CONTROLLA(ph_cfg_telefono_in_testo(t, &b) == 0 && strstr((char *)b.dati, "[note]") == NULL);
        strcpy(r->note, "residuo");
        CONTROLLA(leggi_tel((char *)b.dati, b.lunghezza, r, &e) == 0 && r->volume == 37 && r->note[0] == '\0');
    }
    /* un solo controllo in mezzo al testo: niente tabella (come la rilettura darebbe vuoto) */
    ph_cfg_telefono_predefinito(t);
    strcpy(t->note, "ciao\x02 mondo");
    ph_buffer_svuota(&b);
    CONTROLLA(ph_cfg_telefono_in_testo(t, &b) == 0 && strstr((char *)b.dati, "[note]") == NULL);
    /* caso peggiore valido: 2047 caratteri tutti da raddoppiare, si rileggono identici */
    const char raddoppiano[] = {'"', '\\', '\n', '\t', '\r'};
    for (size_t i = 0; i < sizeof raddoppiano; i++) {
        ph_cfg_telefono_predefinito(t);
        t->volume = 37;
        memset(t->note, raddoppiano[i], PH_CFG_LUN_NOTE - 1);
        t->note[PH_CFG_LUN_NOTE - 1] = '\0';
        if (raddoppiano[i] == '\r') { /* il \r si toglie in lettura: nota vuota, profilo intero */
            ph_buffer_svuota(&b);
            CONTROLLA(ph_cfg_telefono_in_testo(t, &b) == 0 && leggi_tel((char *)b.dati, b.lunghezza, r, &e) == 0 &&
                      r->volume == 37 && r->note[0] == '\0');
        } else {
            CONTROLLA(note_andata_ritorno(t, r, &b, &e) && r->volume == 37 && strlen(r->note) == PH_CFG_LUN_NOTE - 1);
        }
    }
    /* su disco: salva e carica, identico; un file con la tabella rovinata non nomina il testo */
    imposta_ambiente("note");
    ph_cfg_telefono_predefinito(t);
    strcpy(t->note, "Segreto\nsu\ndisco \xC3\xA8");
    CONTROLLA(ph_cfg_telefono_salva("R5CT0000000", t, &e) == 0 && ph_cfg_telefono_carica("R5CT0000000", r, &e) == 0 &&
              uguali_telefono(t, r) && strcmp(r->note, t->note) == 0);
    char pf[800];
    CONTROLLA(ph_cfg_percorso_telefono("R5CT0000000", pf, sizeof pf, &e) == 0);
    const char *rotto2 = "[note]\ntesto = \"Segreto non chiuso\n";
    CONTROLLA(ph_cfg_scrivi_file(pf, rotto2, strlen(rotto2), &e) == 0);
    CONTROLLA(ph_cfg_telefono_carica("R5CT0000000", r, &e) == 1 && strstr(e.testo, "Segreto") == NULL && r->note[0] == '\0');
    ph_buffer_libera(&b);
    ph_cfg_telefono_libera(t);
    ph_cfg_telefono_libera(r);
}

/* ---------- app in "Compatibilita'" ---------- */

static void prova_compatibilita(void)
{
    PhErrore e = {{0}};
    PhCfgTelefono *t = ph_cfg_telefono_nuovo(), *r = ph_cfg_telefono_nuovo();
    PhBuffer b = PH_BUFFER_VUOTO;
    CONTROLLA(t->n_compatibilita == 0 && !ph_cfg_telefono_in_compatibilita(t, "com.a") && !ph_cfg_telefono_in_compatibilita(t, NULL));
    /* attiva e disattiva; gia' nello stato chiesto = 0 senza cambi */
    CONTROLLA(ph_cfg_telefono_imposta_compatibilita(t, "com.a", true, &e) == 0 &&
              ph_cfg_telefono_imposta_compatibilita(t, "com.b", true, &e) == 0 &&
              ph_cfg_telefono_imposta_compatibilita(t, "com.c", true, &e) == 0 && t->n_compatibilita == 3);
    CONTROLLA(ph_cfg_telefono_imposta_compatibilita(t, "com.b", true, &e) == 0 && t->n_compatibilita == 3);
    CONTROLLA(ph_cfg_telefono_imposta_compatibilita(t, "com.assente", false, &e) == 0 && t->n_compatibilita == 3);
    CONTROLLA(ph_cfg_telefono_in_compatibilita(t, "com.b") && !ph_cfg_telefono_in_compatibilita(t, "com") &&
              !ph_cfg_telefono_in_compatibilita(t, "com.bb"));
    /* togliere in mezzo: l'ordine delle altre resta */
    CONTROLLA(ph_cfg_telefono_imposta_compatibilita(t, "com.b", false, &e) == 0 && t->n_compatibilita == 2 &&
              strcmp(t->compatibilita[0], "com.a") == 0 && strcmp(t->compatibilita[1], "com.c") == 0 &&
              t->compatibilita[2][0] == '\0' && !ph_cfg_telefono_in_compatibilita(t, "com.b"));
    /* togliere il primo con altre tre dietro: scorrono tutte, nello stesso ordine */
    CONTROLLA(ph_cfg_telefono_imposta_compatibilita(t, "com.d", true, &e) == 0 && ph_cfg_telefono_imposta_compatibilita(t, "com.e", true, &e) == 0);
    CONTROLLA(ph_cfg_telefono_imposta_compatibilita(t, "com.a", false, &e) == 0 && t->n_compatibilita == 3 &&
              strcmp(t->compatibilita[0], "com.c") == 0 && strcmp(t->compatibilita[1], "com.d") == 0 &&
              strcmp(t->compatibilita[2], "com.e") == 0);
    CONTROLLA(ph_cfg_telefono_imposta_compatibilita(t, "com.d", false, &e) == 0 && t->n_compatibilita == 2 &&
              strcmp(t->compatibilita[0], "com.c") == 0 && strcmp(t->compatibilita[1], "com.e") == 0);
    CONTROLLA(ph_cfg_telefono_imposta_compatibilita(t, "com.d", true, &e) == 0 && ph_cfg_telefono_imposta_compatibilita(t, "com.a", true, &e) == 0);
    CONTROLLA(ph_cfg_telefono_imposta_compatibilita(t, "com.e", false, &e) == 0 && ph_cfg_telefono_imposta_compatibilita(t, "com.d", false, &e) == 0 &&
              ph_cfg_telefono_imposta_compatibilita(t, "com.a", false, &e) == 0 && t->n_compatibilita == 1 &&
              strcmp(t->compatibilita[0], "com.c") == 0);
    CONTROLLA(ph_cfg_telefono_imposta_compatibilita(t, "com.a", true, &e) == 0);
    /* nomi non validi */
    char lungo[PH_CFG_LUN_APP + 5];
    memset(lungo, 'x', sizeof lungo - 1);
    lungo[sizeof lungo - 1] = '\0';
    CONTROLLA(ph_cfg_telefono_imposta_compatibilita(t, "", true, &e) < 0 && ph_cfg_telefono_imposta_compatibilita(t, NULL, true, &e) < 0 &&
              ph_cfg_telefono_imposta_compatibilita(t, lungo, true, &e) < 0 && ph_cfg_telefono_imposta_compatibilita(t, "", false, &e) < 0 &&
              t->n_compatibilita == 2);
    /* elenco pieno: -1 col messaggio tradotto, nessuna voce tolta; togliere e' sempre possibile */
    t->n_compatibilita = 0;
    for (int i = 0; i < PH_CFG_MAX_COMPAT; i++) {
        char n[32];
        snprintf(n, sizeof n, "com.app%03d", i);
        CONTROLLA(ph_cfg_telefono_imposta_compatibilita(t, n, true, &e) == 0);
    }
    ph_lingua_imposta(PH_LINGUA_IT);
    CONTROLLA(ph_cfg_telefono_imposta_compatibilita(t, "com.una.di.troppo", true, &e) == -1 &&
              strcmp(e.testo, "Troppe app in compatibilit\xC3\xA0.") == 0);
    ph_lingua_imposta(PH_LINGUA_EN);
    CONTROLLA(ph_cfg_telefono_imposta_compatibilita(t, "com.una.di.troppo", true, &e) == -1 &&
              strcmp(e.testo, "Too many apps in compatibility mode.") == 0);
    ph_lingua_imposta(PH_LINGUA_IT);
    CONTROLLA(t->n_compatibilita == PH_CFG_MAX_COMPAT && strcmp(t->compatibilita[0], "com.app000") == 0 &&
              !ph_cfg_telefono_in_compatibilita(t, "com.una.di.troppo"));
    CONTROLLA(ph_cfg_telefono_imposta_compatibilita(t, "com.app005", true, &e) == 0); /* presente: nessun errore */
    CONTROLLA(ph_cfg_telefono_imposta_compatibilita(t, "com.app005", false, &e) == 0 && t->n_compatibilita == PH_CFG_MAX_COMPAT - 1);
    CONTROLLA(ph_cfg_telefono_imposta_compatibilita(t, "com.una.di.troppo", true, &e) == 0 && t->n_compatibilita == PH_CFG_MAX_COMPAT);
    /* struttura riempita male: n_compatibilita fuori misura si riporta a 128 */
    t->n_compatibilita = 100000;
    CONTROLLA(ph_cfg_telefono_imposta_compatibilita(t, "com.una.di.troppo", false, &e) == 0 && t->n_compatibilita == PH_CFG_MAX_COMPAT - 1);

    /* andata e ritorno */
    t->n_compatibilita = 0;
    CONTROLLA(ph_cfg_telefono_imposta_compatibilita(t, "com.uno", true, &e) == 0 &&
              ph_cfg_telefono_imposta_compatibilita(t, "org.esempio.\xC3\xA8", true, &e) == 0);
    CONTROLLA(ph_cfg_telefono_in_testo(t, &b) == 0 && strstr((char *)b.dati, "[[compatibilita]]\napp = \"com.uno\"\n") != NULL);
    CONTROLLA(leggi_tel((char *)b.dati, b.lunghezza, r, &e) == 0 && uguali_telefono(t, r) && r->n_compatibilita == 2);
    /* file vecchio: vuoto */
    const char *vecchio = "[audio]\nvolume = 10\n[[fissata]]\napp = \"com.a\"\n";
    CONTROLLA(leggi_tel(vecchio, strlen(vecchio), r, &e) == 0 && r->n_compatibilita == 0);
    /* file ostili: non valide, doppie, tipo sbagliato, troppo lunghe; le altre restano, in ordine */
    PhBuffer l = PH_BUFFER_VUOTO;
    const char *testa = "[[compatibilita]]\napp = \"uno\"\n[[compatibilita]]\napp = \"\"\n[[compatibilita]]\napp = 5\n"
                        "[[compatibilita]]\n[[compatibilita]]\napp = \"uno\"\n[[compatibilita]]\napp = \"due\"\n"
                        "[[compatibilita]]\napp = \"";
    ph_buffer_aggiungi(&l, testa, strlen(testa));
    for (int i = 0; i < 500; i++)
        ph_buffer_aggiungi(&l, "z", 1);
    const char *coda = "\"\n[[compatibilita]]\napp = \"tre\"\n";
    ph_buffer_aggiungi(&l, coda, strlen(coda));
    CONTROLLA(leggi_tel((char *)l.dati, l.lunghezza, r, &e) == 0 && r->n_compatibilita == 3 && strcmp(r->compatibilita[0], "uno") == 0 &&
              strcmp(r->compatibilita[1], "due") == 0 && strcmp(r->compatibilita[2], "tre") == 0);
    /* oltre 128: si tengono le prime */
    ph_buffer_svuota(&l);
    for (int i = 0; i < 200; i++) {
        char riga[64];
        int k = snprintf(riga, sizeof riga, "[[compatibilita]]\napp = \"q%03d\"\n", i);
        ph_buffer_aggiungi(&l, riga, (size_t)k);
    }
    CONTROLLA(leggi_tel((char *)l.dati, l.lunghezza, r, &e) == 0 && r->n_compatibilita == PH_CFG_MAX_COMPAT &&
              strcmp(r->compatibilita[127], "q127") == 0);
    ph_buffer_libera(&l);
    /* file rovinato: predefiniti */
    const char *rotto = "[[compatibilita]]\napp = \"aperta\n";
    CONTROLLA(leggi_tel(rotto, strlen(rotto), r, &e) < 0 && r->n_compatibilita == 0 && e_predefinito(r));
    ph_buffer_libera(&b);
    ph_cfg_telefono_libera(t);
    ph_cfg_telefono_libera(r);
}

int main(void)
{
    snprintf(radice, sizeof radice, "/tmp/prova_config_XXXXXX");
    if (mkdtemp(radice) == NULL) {
        perror("mkdtemp");
        return 2;
    }
    /* da qui in poi HOME e XDG_* puntano alla cartella temporanea */
    imposta_ambiente("iniziale");
    prova_toml();
    prova_andata_ritorno();
    prova_rovinati();
    prova_percorsi();
    prova_file();
    prova_cartella_e_temporanei();
    prova_senza_home();
    prova_pulisci_sfondi();
    prova_fissate();
    prova_stile_widget();
    prova_misure_widget();
    prova_aperture();
    prova_compatibilita();
    prova_meteo_luogo();
    prova_note();
    prova_decimali_virgola();
    nftw(radice, rm_voce, 16, FTW_DEPTH | FTW_PHYS);
    if (errori == 0)
        printf("prova_config: tutto bene\n");
    return errori == 0 ? 0 : 1;
}
