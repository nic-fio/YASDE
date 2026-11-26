/* Strutture di configurazione: da testo a struttura e ritorno (vedi config.h).
 * La lettura parte sempre dai valori predefiniti e sovrascrive solo i campi
 * validi: un campo sbagliato resta al predefinito. */
#include "config/config.h"

#include <dirent.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <sys/stat.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>

#include "config/config_interno.h"
#include "config/lingua.h"
#include "config/toml.h"

/* ---------- lettura di campi ---------- */

static void leggi_bool(const PhTomlTabella *t, const char *k, bool *dst)
{
    const PhTomlVoce *v = ph_toml_voce(t, k, PH_TOML_BOOLEANO);
    if (v != NULL)
        *dst = v->b;
}

/* Intero in [min, max]; altrimenti *dst non cambia. Torna 1 se letto. */
static int leggi_intero(const PhTomlTabella *t, const char *k, int64_t min, int64_t max, int *dst)
{
    const PhTomlVoce *v = ph_toml_voce(t, k, PH_TOML_INTERO);
    if (v == NULL || v->i < min || v->i > max)
        return 0;
    *dst = (int)v->i;
    return 1;
}

/* Misura libera di un widget: 0 oppure 48-2048. Qualunque altro valore (o un tipo
 * sbagliato) lascia 0, "non salvata", senza far fallire il caricamento. */
static void leggi_misura(const PhTomlTabella *t, const char *k, int *dst)
{
    int x = 0;
    if (leggi_intero(t, k, 0, 2048, &x) && (x == 0 || x >= 48))
        *dst = x;
}

static void leggi_decimale(const PhTomlTabella *t, const char *k, double min, double max, double *dst)
{
    const PhTomlVoce *v = ph_toml_voce(t, k, PH_TOML_DECIMALE);
    double x;
    if (v != NULL)
        x = v->d;
    else if ((v = ph_toml_voce(t, k, PH_TOML_INTERO)) != NULL)
        x = (double)v->i; /* "scala = 2" e' ragionevole */
    else
        return;
    if (x >= min && x <= max)
        *dst = x;
}

/* Stringa che sta nel buffer (terminatore compreso); altrimenti non cambia.
 * Torna 1 se letta. */
static int leggi_stringa(const PhTomlTabella *t, const char *k, char *dst, size_t cap)
{
    const PhTomlVoce *v = ph_toml_voce(t, k, PH_TOML_STRINGA);
    if (v == NULL || strlen(v->s) >= cap)
        return 0;
    memcpy(dst, v->s, strlen(v->s) + 1);
    return 1;
}

/* Valore tra i nomi dati; altrimenti *dst non cambia. Torna 1 se letto. */
static int leggi_scelta(const PhTomlTabella *t, const char *k, const char *const *nomi, size_t n, int *dst)
{
    const PhTomlVoce *v = ph_toml_voce(t, k, PH_TOML_STRINGA);
    if (v == NULL)
        return 0;
    for (size_t i = 0; i < n; i++) {
        if (strcmp(v->s, nomi[i]) == 0) {
            *dst = (int)i;
            return 1;
        }
    }
    return 0;
}

/* "#rrggbb" (solo esadecimali, sei cifre). */
static void leggi_colore(const PhTomlTabella *t, const char *k, uint32_t *dst)
{
    const PhTomlVoce *v = ph_toml_voce(t, k, PH_TOML_STRINGA);
    if (v == NULL || strlen(v->s) != 7 || v->s[0] != '#')
        return;
    uint32_t c = 0;
    for (int i = 1; i < 7; i++) {
        char h = v->s[i];
        int d;
        if (h >= '0' && h <= '9')
            d = h - '0';
        else if (h >= 'a' && h <= 'f')
            d = h - 'a' + 10;
        else if (h >= 'A' && h <= 'F')
            d = h - 'A' + 10;
        else
            return;
        c = (c << 4) | (uint32_t)d;
    }
    *dst = c;
}

/* Prima tabella (non ripetuta) con quel nome, NULL se manca. */
static const PhTomlTabella *tabella(const PhToml *doc, const char *nome)
{
    for (size_t i = 1; i < doc->n; i++)
        if (!doc->tabelle[i].ripetuta && strcmp(doc->tabelle[i].nome, nome) == 0)
            return &doc->tabelle[i];
    return NULL;
}

/* ---------- scrittura di campi ---------- */

#define SCRIVI(chiamata)        \
    do {                        \
        if ((chiamata) < 0)     \
            return -1;          \
    } while (0)

static int scrivi_colore(PhBuffer *b, const char *k, uint32_t c)
{
    char tmp[8];
    snprintf(tmp, sizeof tmp, "#%06x", (unsigned)(c & 0xFFFFFF));
    return ph_toml_scrivi_stringa(b, k, tmp);
}

/* Elemento ripetuto: l'indice nella lista dei nomi. */
static const char *const NOMI_LINGUA[] = {"auto", "it", "en"};
static const char *const NOMI_SFONDO[] = {"solido", "sfumato", "immagine"};
static const char *const NOMI_DIREZIONE[] = {"diagonale", "verticale", "orizzontale", "diagonale-inversa"};
static const char *const NOMI_ADATT[] = {"riempi", "adatta", "allunga", "centra"};
static const char *const NOMI_WIDGET[] = {"orologio", "stato",  "notifiche", "memoria",
                                          "calendario", "note", "meteo",     "riproduzione",
                                          "calendario-mese", "sveglia", "batteria", "interruttori",
                                          "trova", "app-frequenti", "trasferimenti"};
static const char *const NOMI_DIM[] = {"piccolo", "medio", "grande"};
#define N(a) (sizeof(a) / sizeof((a)[0]))

/* Nome di un valore di enumerazione; un valore fuori elenco (struttura
 * riempita male da chi chiama) non deve leggere fuori dalla tabella. */
static const char *nome_di(const char *const *nomi, size_t n, int v)
{
    return v >= 0 && (size_t)v < n ? nomi[v] : nomi[0];
}

/* ---------- caricamento comune ---------- */

#define MAX_ILLEGGIBILI 10

/* Rinomina il file in "<file>.illeggibile[.N]" senza sostituire un file che
 * c'e' gia'; se i 10 nomi sono presi sostituisce il piu' vecchio. */
int ph_cfg_metti_da_parte(const char *percorso, char *nuovo, size_t cap)
{
    time_t piu_vecchio = 0;
    int idx_vecchio = -1;
    for (int k = 0; k < MAX_ILLEGGIBILI; k++) {
        int n = k == 0 ? snprintf(nuovo, cap, "%s.illeggibile", percorso)
                       : snprintf(nuovo, cap, "%s.illeggibile.%d", percorso, k);
        if (n < 0 || (size_t)n >= cap)
            return -1;
        if (renameat2(AT_FDCWD, percorso, AT_FDCWD, nuovo, RENAME_NOREPLACE) == 0)
            return 0;
        if (errno == EINVAL || errno == ENOSYS) /* il filesystem non lo sa fare */
            return rename(percorso, nuovo);
        if (errno != EEXIST)
            return -1;
        struct stat st;
        if (stat(nuovo, &st) == 0 && (idx_vecchio < 0 || st.st_mtime < piu_vecchio)) {
            piu_vecchio = st.st_mtime;
            idx_vecchio = k;
        }
    }
    if (idx_vecchio < 0)
        idx_vecchio = 0;
    int n = idx_vecchio == 0 ? snprintf(nuovo, cap, "%s.illeggibile", percorso)
                             : snprintf(nuovo, cap, "%s.illeggibile.%d", percorso, idx_vecchio);
    if (n < 0 || (size_t)n >= cap)
        return -1;
    return rename(percorso, nuovo);
}

/* Legge il file e lo interpreta. Se esiste ma non e' leggibile (sintassi o
 * troppo grande) lo mette da parte come "<file>.illeggibile" e torna 1; vedi
 * ESITI in config.h. `predefinito` rimette i valori di partenza. */
int ph_cfg_carica_file(const char *percorso, const char *etichetta, size_t massimo, DaTesto da_testo, void *dst,
                       void (*predefinito)(void *), PhErrore *e)
{
    predefinito(dst);
    PhBuffer b = PH_BUFFER_VUOTO;
    int r = ph_cfg_leggi_file(percorso, massimo, &b, e);
    if (r == 0) {
        int d = da_testo((const char *)b.dati, b.lunghezza, dst, e);
        if (d == -2)
            r = 4; /* da non toccare */
        else if (d < 0)
            r = 3; /* sintassi */
    }
    ph_buffer_libera(&b);
    if (r == 0 || r == 1)
        return 0;
    if (r == 4) {
        predefinito(dst);
        ph_errore_contesto(e, "%s", etichetta);
        return -1;
    }
    if (r < 0) {
        ph_errore_contesto(e, "%s", etichetta);
        return -1;
    }
    predefinito(dst);
    char motivo[sizeof e->testo] = "";
    if (e != NULL)
        memcpy(motivo, e->testo, sizeof motivo);
    char nuovo[PATH_MAX];
    if (ph_cfg_metti_da_parte(percorso, nuovo, sizeof nuovo) < 0) {
        ph_errore(e, "%s: %s; impossibile metterlo da parte", etichetta, motivo);
        return -1;
    }
    const char *base = strrchr(nuovo, '/');
    ph_errore(e, "%s: %s; messo da parte come %s", etichetta, motivo, base != NULL ? base + 1 : nuovo);
    return 1;
}

static int dt_pref(const char *t, size_t n, void *d, PhErrore *e)
{
    return ph_cfg_preferenze_da_testo(t, n, d, e);
}
static void pr_pref(void *d) { ph_cfg_preferenze_predefinite(d); }
static int dt_tels(const char *t, size_t n, void *d, PhErrore *e)
{
    return ph_cfg_telefoni_da_testo(t, n, d, e);
}
static void pr_tels(void *d) { ph_cfg_telefoni_predefiniti(d); }
static int dt_tel(const char *t, size_t n, void *d, PhErrore *e)
{
    return ph_cfg_telefono_da_testo(t, n, d, e);
}
static void pr_tel(void *d) { ph_cfg_telefono_predefinito(d); }

/* ---------- preferenze ---------- */

void ph_cfg_preferenze_predefinite(PhCfgPreferenze *p)
{
    *p = (PhCfgPreferenze){PH_CFG_LINGUA_AUTO, true, true, "", false, false, true, 1.0};
}

int ph_cfg_preferenze_da_testo(const char *testo, size_t lunghezza, PhCfgPreferenze *p, PhErrore *e)
{
    ph_cfg_preferenze_predefinite(p);
    PhToml doc;
    if (ph_toml_leggi(testo, lunghezza, &doc, e) < 0)
        return -1;
    const PhTomlTabella *g = tabella(&doc, "generale");
    if (g != NULL) {
        int l;
        if (leggi_scelta(g, "lingua", NOMI_LINGUA, N(NOMI_LINGUA), &l))
            p->lingua = (PhCfgLingua)l;
        leggi_bool(g, "esc_indietro", &p->esc_indietro);
        leggi_bool(g, "avvisi", &p->avvisi);
        leggi_stringa(g, "cartella_ricevuti", p->cartella_ricevuti, sizeof p->cartella_ricevuti);
        leggi_bool(g, "widget_nascosti", &p->widget_nascosti);
        leggi_bool(g, "riapri_dove_eri", &p->riapri_dove_eri);
    }
    const PhTomlTabella *s = tabella(&doc, "shell");
    if (s != NULL) {
        leggi_bool(s, "scala_automatica", &p->scala_automatica);
        leggi_decimale(s, "scala", 0.5, 4.0, &p->scala);
    }
    ph_toml_libera(&doc);
    return 0;
}

int ph_cfg_preferenze_in_testo(const PhCfgPreferenze *p, PhBuffer *out)
{
    SCRIVI(ph_toml_scrivi_commento(out, "Preferenze generali di YASDE. Si puo' modificare a mano."));
    SCRIVI(ph_toml_scrivi_tabella(out, "generale", false));
    SCRIVI(ph_toml_scrivi_stringa(out, "lingua", nome_di(NOMI_LINGUA, N(NOMI_LINGUA), (int)p->lingua)));
    SCRIVI(ph_toml_scrivi_booleano(out, "esc_indietro", p->esc_indietro));
    SCRIVI(ph_toml_scrivi_booleano(out, "avvisi", p->avvisi));
    SCRIVI(ph_toml_scrivi_stringa(out, "cartella_ricevuti", p->cartella_ricevuti));
    SCRIVI(ph_toml_scrivi_booleano(out, "widget_nascosti", p->widget_nascosti));
    SCRIVI(ph_toml_scrivi_booleano(out, "riapri_dove_eri", p->riapri_dove_eri));
    SCRIVI(ph_toml_scrivi_tabella(out, "shell", false));
    SCRIVI(ph_toml_scrivi_booleano(out, "scala_automatica", p->scala_automatica));
    SCRIVI(ph_toml_scrivi_decimale(out, "scala", p->scala));
    return 0;
}

/* Carica e salva condividono lo schema: percorso, lettura, da_testo. */
int ph_cfg_preferenze_carica(PhCfgPreferenze *t, PhErrore *e)
{
    ph_cfg_preferenze_predefinite(t);
    char percorso[PH_CFG_LUN_PERCORSO + 64];
    if (ph_cfg_percorso_preferenze(percorso, sizeof percorso, e) < 0)
        return -1;
    return ph_cfg_carica_file(percorso, "preferenze.toml", PH_CFG_MAX_FILE, dt_pref, t, pr_pref, e);
}

int ph_cfg_preferenze_salva(const PhCfgPreferenze *p, PhErrore *e)
{
    char percorso[PH_CFG_LUN_PERCORSO + 64];
    if (ph_cfg_percorso_preferenze(percorso, sizeof percorso, e) < 0)
        return -1;
    PhBuffer b = PH_BUFFER_VUOTO;
    int r = ph_cfg_preferenze_in_testo(p, &b);
    if (r < 0)
        ph_errore(e, "memoria esaurita preparando preferenze.toml");
    else
        r = ph_cfg_scrivi_file(percorso, b.dati, b.lunghezza, e);
    ph_buffer_libera(&b);
    return r;
}

/* ---------- telefoni conosciuti ---------- */

void ph_cfg_telefoni_predefiniti(PhCfgTelefoni *t)
{
    memset(t, 0, sizeof *t);
}

#define SECONDI_MAX 4102444800LL /* 1 gen 2100: oltre e' un valore senza senso */

int ph_cfg_telefoni_da_testo(const char *testo, size_t lunghezza, PhCfgTelefoni *t, PhErrore *e)
{
    ph_cfg_telefoni_predefiniti(t);
    PhToml doc;
    if (ph_toml_leggi(testo, lunghezza, &doc, e) < 0)
        return -1;
    for (size_t i = 1; i < doc.n && t->n < PH_CFG_MAX_TELEFONI; i++) {
        const PhTomlTabella *g = &doc.tabelle[i];
        if (!g->ripetuta || strcmp(g->nome, "telefono") != 0)
            continue;
        PhCfgTelefonoNoto v;
        memset(&v, 0, sizeof v);
        if (!leggi_stringa(g, "seriale", v.seriale, sizeof v.seriale) || v.seriale[0] == '\0')
            continue;
        bool doppio = false;
        for (size_t k = 0; k < t->n; k++)
            if (strcmp(t->voci[k].seriale, v.seriale) == 0)
                doppio = true;
        if (doppio)
            continue;
        leggi_stringa(g, "nome", v.nome, sizeof v.nome);
        leggi_stringa(g, "indirizzo", v.indirizzo, sizeof v.indirizzo);
        const PhTomlVoce *visto = ph_toml_voce(g, "visto", PH_TOML_INTERO);
        if (visto != NULL && visto->i >= 0 && visto->i <= SECONDI_MAX)
            v.visto = visto->i;
        t->voci[t->n++] = v;
    }
    ph_toml_libera(&doc);
    return 0;
}

int ph_cfg_telefoni_in_testo(const PhCfgTelefoni *t, PhBuffer *out)
{
    SCRIVI(ph_toml_scrivi_commento(out, "Telefoni conosciuti da YASDE. 'visto' = secondi Unix."));
    for (size_t i = 0; i < t->n && i < PH_CFG_MAX_TELEFONI; i++) {
        SCRIVI(ph_toml_scrivi_tabella(out, "telefono", true));
        SCRIVI(ph_toml_scrivi_stringa(out, "seriale", t->voci[i].seriale));
        SCRIVI(ph_toml_scrivi_stringa(out, "nome", t->voci[i].nome));
        SCRIVI(ph_toml_scrivi_stringa(out, "indirizzo", t->voci[i].indirizzo));
        SCRIVI(ph_toml_scrivi_intero(out, "visto", t->voci[i].visto));
    }
    return 0;
}

int ph_cfg_telefoni_carica(PhCfgTelefoni *t, PhErrore *e)
{
    ph_cfg_telefoni_predefiniti(t);
    char percorso[PH_CFG_LUN_PERCORSO + 64];
    if (ph_cfg_percorso_telefoni(percorso, sizeof percorso, e) < 0)
        return -1;
    return ph_cfg_carica_file(percorso, "telefoni.toml", PH_CFG_MAX_FILE, dt_tels, t, pr_tels, e);
}

int ph_cfg_telefoni_salva(const PhCfgTelefoni *t, PhErrore *e)
{
    char percorso[PH_CFG_LUN_PERCORSO + 64];
    if (ph_cfg_percorso_telefoni(percorso, sizeof percorso, e) < 0)
        return -1;
    PhBuffer b = PH_BUFFER_VUOTO;
    int r = ph_cfg_telefoni_in_testo(t, &b);
    if (r < 0)
        ph_errore(e, "memoria esaurita preparando telefoni.toml");
    else
        r = ph_cfg_scrivi_file(percorso, b.dati, b.lunghezza, e);
    ph_buffer_libera(&b);
    return r;
}

/* ---------- un telefono ---------- */

void ph_cfg_telefono_predefinito(PhCfgTelefono *t)
{
    memset(t, 0, sizeof *t);
    t->sfondo.tipo = PH_CFG_SFONDO_SFUMATO;
    t->sfondo.colore_solido = 0x1f2430; /* mockup: solido #1f2430 */
    t->sfondo.colore_da = 0x0e2638; /* mockup 6.2: 135 gradi da #0e2638 a #1a625f */
    t->sfondo.colore_a = 0x1a625f;
    t->sfondo.direzione = PH_CFG_DIR_DIAGONALE;
    t->sfondo.adattamento = PH_CFG_ADATT_RIEMPI;
    t->volume = 100; /* decisione del committente (24 nov 2026): si parte dal massimo */
    /* Desktop del primo avvio, come nel mockup (5.2.3): tre widget medi. */
    t->widget[0] = (PhCfgWidget){.tipo = PH_CFG_WIDGET_OROLOGIO, .destra = 16, .alto = 16, .dimensione = PH_CFG_WIDGET_MEDIO};
    t->widget[1] = (PhCfgWidget){.tipo = PH_CFG_WIDGET_STATO, .destra = 16, .alto = 120, .dimensione = PH_CFG_WIDGET_MEDIO};
    t->widget[2] = (PhCfgWidget){.tipo = PH_CFG_WIDGET_NOTIFICHE, .destra = 16, .alto = 240, .dimensione = PH_CFG_WIDGET_MEDIO};
    t->n_widget = 3;
}

bool ph_cfg_telefono_in_compatibilita(const PhCfgTelefono *t, const char *app)
{
    if (app == NULL)
        return false;
    for (size_t i = 0; i < t->n_compatibilita && i < PH_CFG_MAX_COMPAT; i++)
        if (strcmp(t->compatibilita[i], app) == 0)
            return true;
    return false;
}

int ph_cfg_telefono_imposta_compatibilita(PhCfgTelefono *t, const char *app, bool attiva, PhErrore *e)
{
    if (app == NULL || app[0] == '\0' || strlen(app) >= PH_CFG_LUN_APP)
        return ph_errore(e, "nome di pacchetto non valido");
    if (t->n_compatibilita > PH_CFG_MAX_COMPAT) /* struttura riempita male */
        t->n_compatibilita = PH_CFG_MAX_COMPAT;
    bool presente = ph_cfg_telefono_in_compatibilita(t, app);
    if (attiva == presente)
        return 0;
    if (attiva) {
        if (t->n_compatibilita >= PH_CFG_MAX_COMPAT)
            return ph_errore(e, "%s", ph_t("Troppe app in compatibilità."));
        memcpy(t->compatibilita[t->n_compatibilita++], app, strlen(app) + 1);
        return 0;
    }
    for (size_t i = 0; i < t->n_compatibilita; i++) {
        if (strcmp(t->compatibilita[i], app) == 0) {
            memmove(t->compatibilita[i], t->compatibilita[i + 1], (t->n_compatibilita - i - 1) * sizeof t->compatibilita[0]);
            t->n_compatibilita--;
            memset(t->compatibilita[t->n_compatibilita], 0, sizeof t->compatibilita[0]);
            break;
        }
    }
    return 0;
}

bool ph_cfg_telefono_app_fissata(const PhCfgTelefono *t, const char *app)
{
    if (app == NULL)
        return false;
    for (size_t i = 0; i < t->n_fissate && i < PH_CFG_MAX_FISSATE; i++)
        if (strcmp(t->fissate[i], app) == 0)
            return true;
    return false;
}

int ph_cfg_telefono_fissa_app(PhCfgTelefono *t, const char *app, PhErrore *e)
{
    if (app == NULL || app[0] == '\0' || strlen(app) >= PH_CFG_LUN_APP)
        return ph_errore(e, "nome di pacchetto non valido");
    if (ph_cfg_telefono_app_fissata(t, app))
        return 0;
    if (t->n_fissate >= PH_CFG_MAX_FISSATE)
        return ph_errore(e, "troppe app fissate sulla barra (al massimo %d)", PH_CFG_MAX_FISSATE);
    memcpy(t->fissate[t->n_fissate++], app, strlen(app) + 1);
    return 0;
}

int ph_cfg_telefono_togli_app(PhCfgTelefono *t, const char *app)
{
    if (app == NULL)
        return 0;
    if (t->n_fissate > PH_CFG_MAX_FISSATE) /* struttura riempita male: si riporta in misura */
        t->n_fissate = PH_CFG_MAX_FISSATE;
    for (size_t i = 0; i < t->n_fissate; i++) {
        if (strcmp(t->fissate[i], app) == 0) {
            memmove(t->fissate[i], t->fissate[i + 1], (t->n_fissate - i - 1) * sizeof t->fissate[0]);
            t->n_fissate--;
            memset(t->fissate[t->n_fissate], 0, sizeof t->fissate[0]);
            return 1;
        }
    }
    return 0;
}

int ph_cfg_telefono_conta_apertura(PhCfgTelefono *t, const char *app, int64_t ora, PhErrore *e)
{
    if (app == NULL || app[0] == '\0' || strlen(app) >= PH_CFG_LUN_APP)
        return ph_errore(e, "nome di pacchetto non valido");
    if (ora < 0 || ora > SECONDI_MAX)
        ora = 0;
    if (t->n_aperture > PH_CFG_MAX_APERTURE) /* struttura riempita male */
        t->n_aperture = PH_CFG_MAX_APERTURE;
    for (size_t i = 0; i < t->n_aperture; i++) {
        if (strcmp(t->aperture[i].app, app) == 0) {
            if (t->aperture[i].volte < PH_CFG_MAX_VOLTE)
                t->aperture[i].volte++;
            t->aperture[i].ultima = ora;
            return 0;
        }
    }
    if (t->n_aperture >= PH_CFG_MAX_APERTURE) {
        /* Si toglie la meno usata; a parita' la piu' vecchia (ultima minore). */
        size_t da_togliere = 0;
        for (size_t i = 1; i < t->n_aperture; i++) {
            const PhCfgApertura *a = &t->aperture[i], *m = &t->aperture[da_togliere];
            if (a->volte < m->volte || (a->volte == m->volte && a->ultima < m->ultima))
                da_togliere = i;
        }
        memmove(&t->aperture[da_togliere], &t->aperture[da_togliere + 1],
                (t->n_aperture - da_togliere - 1) * sizeof t->aperture[0]);
        t->n_aperture--;
    }
    PhCfgApertura *n = &t->aperture[t->n_aperture++];
    memset(n, 0, sizeof *n);
    memcpy(n->app, app, strlen(app) + 1);
    n->volte = 1;
    n->ultima = ora;
    return 0;
}

PhCfgTelefono *ph_cfg_telefono_nuovo(void)
{
    PhCfgTelefono *t = malloc(sizeof *t);
    if (t != NULL)
        ph_cfg_telefono_predefinito(t);
    return t;
}

void ph_cfg_telefono_libera(PhCfgTelefono *t)
{
    free(t);
}

static void leggi_sfondo(const PhTomlTabella *g, PhCfgSfondo *s)
{
    int x;
    if (leggi_scelta(g, "tipo", NOMI_SFONDO, N(NOMI_SFONDO), &x))
        s->tipo = (PhCfgSfondoTipo)x;
    leggi_colore(g, "colore_solido", &s->colore_solido);
    leggi_colore(g, "colore_da", &s->colore_da);
    leggi_colore(g, "colore_a", &s->colore_a);
    if (leggi_scelta(g, "direzione", NOMI_DIREZIONE, N(NOMI_DIREZIONE), &x))
        s->direzione = (PhCfgDirezione)x;
    if (leggi_scelta(g, "adattamento", NOMI_ADATT, N(NOMI_ADATT), &x))
        s->adattamento = (PhCfgAdattamento)x;
    char nome[PH_CFG_LUN_FILE_CACHE];
    if (leggi_stringa(g, "immagine", nome, sizeof nome) && (nome[0] == '\0' || ph_cfg_nome_cache_valido(nome)))
        memcpy(s->immagine, nome, strlen(nome) + 1);
}

int ph_cfg_telefono_da_testo(const char *testo, size_t lunghezza, PhCfgTelefono *t, PhErrore *e)
{
    ph_cfg_telefono_predefinito(t);
    PhToml doc;
    if (ph_toml_leggi(testo, lunghezza, &doc, e) < 0)
        return -1;

    const PhTomlTabella *g;
    if ((g = tabella(&doc, "sfondo")) != NULL)
        leggi_sfondo(g, &t->sfondo);
    t->n_widget = 0; /* un file valido ha l'ultima parola: niente [[widget]] = nessun widget */
    if ((g = tabella(&doc, "audio")) != NULL) {
        leggi_intero(g, "volume", 0, 100, &t->volume);
    }
    if ((g = tabella(&doc, "cartelle")) != NULL) {
        leggi_stringa(g, "invio", t->cartella_invio, sizeof t->cartella_invio);
        leggi_stringa(g, "ricezione", t->cartella_ricezione, sizeof t->cartella_ricezione);
    }
    if ((g = tabella(&doc, "app")) != NULL) {
        /* 0 = densita' del telefono; altrimenti un intervallo ragionevole di dpi. */
        const PhTomlVoce *v = ph_toml_voce(g, "densita", PH_TOML_INTERO);
        if (v != NULL && (v->i == 0 || (v->i >= 72 && v->i <= 960)))
            t->densita_app = (int)v->i;
    }

    for (size_t i = 1; i < doc.n; i++) {
        const PhTomlTabella *r = &doc.tabelle[i];
        if (!r->ripetuta)
            continue;
        if (strcmp(r->nome, "icona") == 0 && t->n_icone < PH_CFG_MAX_ICONE) {
            PhCfgIcona ic;
            memset(&ic, 0, sizeof ic);
            if (!leggi_stringa(r, "app", ic.app, sizeof ic.app) || ic.app[0] == '\0' ||
                !leggi_intero(r, "colonna", 0, 255, &ic.colonna) || !leggi_intero(r, "riga", 0, 255, &ic.riga))
                continue;
            bool doppia = false;
            for (size_t k = 0; k < t->n_icone; k++)
                if (strcmp(t->icone[k].app, ic.app) == 0)
                    doppia = true;
            if (!doppia)
                t->icone[t->n_icone++] = ic;
        } else if (strcmp(r->nome, "widget") == 0 && t->n_widget < PH_CFG_MAX_WIDGET) {
            PhCfgWidget w = {.tipo = PH_CFG_WIDGET_OROLOGIO, .destra = 16, .alto = 16, .dimensione = PH_CFG_WIDGET_MEDIO};
            int x;
            if (!leggi_scelta(r, "tipo", NOMI_WIDGET, N(NOMI_WIDGET), &x))
                continue;
            w.tipo = (PhCfgWidgetTipo)x;
            leggi_intero(r, "destra", 0, 16384, &w.destra);
            leggi_intero(r, "alto", 0, 16384, &w.alto);
            if (leggi_scelta(r, "dimensione", NOMI_DIM, N(NOMI_DIM), &x))
                w.dimensione = (PhCfgWidgetDim)x;
            leggi_misura(r, "larghezza", &w.larghezza);
            leggi_misura(r, "altezza", &w.altezza);
            leggi_intero(r, "stile", 0, 15, &w.stile);
            leggi_intero(r, "opzione", 0, 255, &w.opzione);
            t->widget[t->n_widget++] = w;
        } else if (strcmp(r->nome, "apertura") == 0 && t->n_aperture < PH_CFG_MAX_APERTURE) {
            PhCfgApertura a;
            memset(&a, 0, sizeof a);
            const PhTomlVoce *volte = ph_toml_voce(r, "volte", PH_TOML_INTERO);
            if (!leggi_stringa(r, "app", a.app, sizeof a.app) || a.app[0] == '\0' || volte == NULL || volte->i < 1 ||
                volte->i > PH_CFG_MAX_VOLTE)
                continue;
            a.volte = (uint32_t)volte->i;
            const PhTomlVoce *ult = ph_toml_voce(r, "ultima", PH_TOML_INTERO);
            if (ult != NULL && ult->i >= 0 && ult->i <= SECONDI_MAX)
                a.ultima = ult->i;
            bool doppia = false;
            for (size_t k = 0; k < t->n_aperture; k++)
                if (strcmp(t->aperture[k].app, a.app) == 0)
                    doppia = true;
            if (!doppia)
                t->aperture[t->n_aperture++] = a;
        } else if (strcmp(r->nome, "compatibilita") == 0 && t->n_compatibilita < PH_CFG_MAX_COMPAT) {
            char app[PH_CFG_LUN_APP];
            if (!leggi_stringa(r, "app", app, sizeof app) || app[0] == '\0' || ph_cfg_telefono_in_compatibilita(t, app))
                continue; /* non valida o doppia */
            memcpy(t->compatibilita[t->n_compatibilita++], app, strlen(app) + 1);
        } else if (strcmp(r->nome, "fissata") == 0 && t->n_fissate < PH_CFG_MAX_FISSATE) {
            char app[PH_CFG_LUN_APP];
            if (!leggi_stringa(r, "app", app, sizeof app) || app[0] == '\0' || ph_cfg_telefono_app_fissata(t, app))
                continue; /* non valida o doppia */
            memcpy(t->fissate[t->n_fissate++], app, strlen(app) + 1);
        } else if (strcmp(r->nome, "finestra") == 0 && t->n_finestre < PH_CFG_MAX_FINESTRE) {
            PhCfgFinestra f;
            memset(&f, 0, sizeof f);
            if (!leggi_stringa(r, "app", f.app, sizeof f.app) || f.app[0] == '\0' ||
                !leggi_intero(r, "x", -32768, 32767, &f.x) || !leggi_intero(r, "y", -32768, 32767, &f.y) ||
                !leggi_intero(r, "larghezza", 1, 16384, &f.larghezza) ||
                !leggi_intero(r, "altezza", 1, 16384, &f.altezza))
                continue;
            bool doppia = false;
            for (size_t k = 0; k < t->n_finestre; k++)
                if (strcmp(t->finestre[k].app, f.app) == 0)
                    doppia = true;
            if (!doppia)
                t->finestre[t->n_finestre++] = f;
        }
    }
    /* Citta' del meteo: valida solo se completa e in limiti; altrimenti non scelta. */
    if ((g = tabella(&doc, "meteo")) != NULL) {
        char nome[sizeof t->meteo_luogo_nome], dett[sizeof t->meteo_luogo_dettaglio] = "";
        const PhTomlVoce *lat = ph_toml_voce(g, "lat_centesimi", PH_TOML_INTERO);
        const PhTomlVoce *lon = ph_toml_voce(g, "lon_centesimi", PH_TOML_INTERO);
        const PhTomlVoce *vd = ph_toml_voce(g, "dettaglio", PH_TOML_STRINGA);
        bool dett_ok = vd == NULL || strlen(vd->s) < sizeof dett;
        if (leggi_stringa(g, "nome", nome, sizeof nome) && nome[0] != '\0' && dett_ok && lat != NULL && lon != NULL &&
            lat->i >= -9000 && lat->i <= 9000 && lon->i >= -18000 && lon->i <= 18000) {
            if (vd != NULL)
                memcpy(dett, vd->s, strlen(vd->s) + 1);
            t->meteo_luogo_scelto = true;
            memcpy(t->meteo_luogo_nome, nome, strlen(nome) + 1);
            memcpy(t->meteo_luogo_dettaglio, dett, strlen(dett) + 1);
            t->meteo_luogo_lat_centesimi = (int32_t)lat->i;
            t->meteo_luogo_lon_centesimi = (int32_t)lon->i;
        }
    }
    ph_toml_libera(&doc);
    return 0;
}

int ph_cfg_telefono_in_testo(const PhCfgTelefono *t, PhBuffer *out)
{
    SCRIVI(ph_toml_scrivi_commento(out, "Impostazioni di un telefono. Si puo' modificare a mano."));
    SCRIVI(ph_toml_scrivi_tabella(out, "sfondo", false));
    SCRIVI(ph_toml_scrivi_stringa(out, "tipo", nome_di(NOMI_SFONDO, N(NOMI_SFONDO), (int)t->sfondo.tipo)));
    SCRIVI(scrivi_colore(out, "colore_solido", t->sfondo.colore_solido));
    SCRIVI(scrivi_colore(out, "colore_da", t->sfondo.colore_da));
    SCRIVI(scrivi_colore(out, "colore_a", t->sfondo.colore_a));
    SCRIVI(ph_toml_scrivi_stringa(out, "direzione", nome_di(NOMI_DIREZIONE, N(NOMI_DIREZIONE), (int)t->sfondo.direzione)));
    SCRIVI(ph_toml_scrivi_stringa(out, "immagine", t->sfondo.immagine));
    SCRIVI(ph_toml_scrivi_stringa(out, "adattamento", nome_di(NOMI_ADATT, N(NOMI_ADATT), (int)t->sfondo.adattamento)));
    SCRIVI(ph_toml_scrivi_tabella(out, "audio", false));
    SCRIVI(ph_toml_scrivi_intero(out, "volume", t->volume));
    SCRIVI(ph_toml_scrivi_tabella(out, "cartelle", false));
    SCRIVI(ph_toml_scrivi_stringa(out, "invio", t->cartella_invio));
    SCRIVI(ph_toml_scrivi_stringa(out, "ricezione", t->cartella_ricezione));
    SCRIVI(ph_toml_scrivi_tabella(out, "app", false));
    SCRIVI(ph_toml_scrivi_intero(out, "densita", t->densita_app));
    for (size_t i = 0; i < t->n_icone && i < PH_CFG_MAX_ICONE; i++) {
        SCRIVI(ph_toml_scrivi_tabella(out, "icona", true));
        SCRIVI(ph_toml_scrivi_stringa(out, "app", t->icone[i].app));
        SCRIVI(ph_toml_scrivi_intero(out, "colonna", t->icone[i].colonna));
        SCRIVI(ph_toml_scrivi_intero(out, "riga", t->icone[i].riga));
    }
    for (size_t i = 0; i < t->n_widget && i < PH_CFG_MAX_WIDGET; i++) {
        SCRIVI(ph_toml_scrivi_tabella(out, "widget", true));
        SCRIVI(ph_toml_scrivi_stringa(out, "tipo", nome_di(NOMI_WIDGET, N(NOMI_WIDGET), (int)t->widget[i].tipo)));
        SCRIVI(ph_toml_scrivi_intero(out, "destra", t->widget[i].destra));
        SCRIVI(ph_toml_scrivi_intero(out, "alto", t->widget[i].alto));
        SCRIVI(ph_toml_scrivi_stringa(out, "dimensione", nome_di(NOMI_DIM, N(NOMI_DIM), (int)t->widget[i].dimensione)));
        /* misure libere: solo se salvate (0 = vale `dimensione`) */
        if (t->widget[i].larghezza != 0)
            SCRIVI(ph_toml_scrivi_intero(out, "larghezza", t->widget[i].larghezza));
        if (t->widget[i].altezza != 0)
            SCRIVI(ph_toml_scrivi_intero(out, "altezza", t->widget[i].altezza));
        SCRIVI(ph_toml_scrivi_intero(out, "stile", t->widget[i].stile));
        SCRIVI(ph_toml_scrivi_intero(out, "opzione", t->widget[i].opzione));
    }
    /* Solo se scelta e valida (un valore non valido non si scrive: si rileggerebbe come non scelta). */
    if (t->meteo_luogo_scelto && t->meteo_luogo_nome[0] != '\0' && t->meteo_luogo_lat_centesimi >= -9000 &&
        t->meteo_luogo_lat_centesimi <= 9000 && t->meteo_luogo_lon_centesimi >= -18000 &&
        t->meteo_luogo_lon_centesimi <= 18000) {
        SCRIVI(ph_toml_scrivi_tabella(out, "meteo", false));
        SCRIVI(ph_toml_scrivi_stringa(out, "nome", t->meteo_luogo_nome));
        SCRIVI(ph_toml_scrivi_stringa(out, "dettaglio", t->meteo_luogo_dettaglio));
        SCRIVI(ph_toml_scrivi_intero(out, "lat_centesimi", t->meteo_luogo_lat_centesimi));
        SCRIVI(ph_toml_scrivi_intero(out, "lon_centesimi", t->meteo_luogo_lon_centesimi));
    }
    for (size_t i = 0; i < t->n_aperture && i < PH_CFG_MAX_APERTURE; i++) {
        SCRIVI(ph_toml_scrivi_tabella(out, "apertura", true));
        SCRIVI(ph_toml_scrivi_stringa(out, "app", t->aperture[i].app));
        SCRIVI(ph_toml_scrivi_intero(out, "volte", t->aperture[i].volte));
        SCRIVI(ph_toml_scrivi_intero(out, "ultima", t->aperture[i].ultima));
    }
    for (size_t i = 0; i < t->n_compatibilita && i < PH_CFG_MAX_COMPAT; i++) {
        SCRIVI(ph_toml_scrivi_tabella(out, "compatibilita", true));
        SCRIVI(ph_toml_scrivi_stringa(out, "app", t->compatibilita[i]));
    }
    for (size_t i = 0; i < t->n_fissate && i < PH_CFG_MAX_FISSATE; i++) {
        SCRIVI(ph_toml_scrivi_tabella(out, "fissata", true));
        SCRIVI(ph_toml_scrivi_stringa(out, "app", t->fissate[i]));
    }
    for (size_t i = 0; i < t->n_finestre && i < PH_CFG_MAX_FINESTRE; i++) {
        SCRIVI(ph_toml_scrivi_tabella(out, "finestra", true));
        SCRIVI(ph_toml_scrivi_stringa(out, "app", t->finestre[i].app));
        SCRIVI(ph_toml_scrivi_intero(out, "x", t->finestre[i].x));
        SCRIVI(ph_toml_scrivi_intero(out, "y", t->finestre[i].y));
        SCRIVI(ph_toml_scrivi_intero(out, "larghezza", t->finestre[i].larghezza));
        SCRIVI(ph_toml_scrivi_intero(out, "altezza", t->finestre[i].altezza));
    }
    return 0;
}

int ph_cfg_telefono_carica(const char *seriale, PhCfgTelefono *t, PhErrore *e)
{
    ph_cfg_telefono_predefinito(t);
    char percorso[PH_CFG_LUN_PERCORSO + 64];
    if (ph_cfg_percorso_telefono(seriale, percorso, sizeof percorso, e) < 0)
        return -1;
    return ph_cfg_carica_file(percorso, "file del telefono", PH_CFG_MAX_FILE, dt_tel, t, pr_tel, e);
}

int ph_cfg_telefono_salva(const char *seriale, const PhCfgTelefono *t, PhErrore *e)
{
    char percorso[PH_CFG_LUN_PERCORSO + 64];
    if (ph_cfg_percorso_telefono(seriale, percorso, sizeof percorso, e) < 0)
        return -1;
    PhBuffer b = PH_BUFFER_VUOTO;
    int r = ph_cfg_telefono_in_testo(t, &b);
    if (r < 0)
        ph_errore(e, "memoria esaurita preparando il file del telefono");
    else
        r = ph_cfg_scrivi_file(percorso, b.dati, b.lunghezza, e);
    ph_buffer_libera(&b);
    return r;
}

/* ---------- pulizia degli sfondi vecchi ---------- */

#define MAX_PROFILI_SFONDI 1024
#define ETA_MINIMA_SFONDO 600 /* secondi */

struct dirent *(*ph_cfg_readdir_prova)(DIR *) = NULL;

/* readdir con errno azzerato: NULL con errno != 0 e' un errore, non la fine. */
static struct dirent *leggi_voce(DIR *d)
{
    errno = 0;
    if (ph_cfg_readdir_prova != NULL)
        return ph_cfg_readdir_prova(d);
    return readdir(d);
}

/* "sfondo-<1-10 cifre>.png", esattamente. */
static bool nome_sfondo(const char *n)
{
    if (strncmp(n, "sfondo-", 7) != 0)
        return false;
    size_t c = 0;
    const char *p = n + 7;
    while (p[c] >= '0' && p[c] <= '9')
        c++;
    return c >= 1 && c <= 10 && strcmp(p + c, ".png") == 0;
}

static bool nome_profilo(const char *n)
{
    size_t l = strlen(n);
    return l > 15 && strncmp(n, "telefono-", 9) == 0 && strcmp(n + l - 5, ".toml") == 0;
}

int ph_cfg_pulisci_sfondi(PhErrore *e)
{
    char dir[PATH_MAX], cache[PATH_MAX];
    if (ph_cfg_cartella_config(dir, sizeof dir, e) < 0 || ph_cfg_cartella_cache(cache, sizeof cache, e) < 0)
        return -1;
    DIR *dc = opendir(dir);
    if (dc == NULL) {
        if (errno == ENOENT)
            return 0; /* nessun profilo conosciuto: non si cancella niente */
        return ph_errore(e, "impossibile aprire %s: %s", dir, strerror(errno));
    }
    char(*rif)[PH_CFG_LUN_FILE_CACHE] = malloc(sizeof *rif * MAX_PROFILI_SFONDI);
    PhCfgTelefono *t = ph_cfg_telefono_nuovo();
    if (rif == NULL || t == NULL) {
        free(rif);
        ph_cfg_telefono_libera(t);
        closedir(dc);
        return ph_errore(e, "memoria esaurita");
    }
    size_t n_rif = 0, n_profili = 0;
    int esito = 0;
    struct dirent *v;
    while (esito == 0 && (v = leggi_voce(dc)) != NULL) {
        if (!nome_profilo(v->d_name))
            continue;
        char p[PATH_MAX];
        if (snprintf(p, sizeof p, "%s/%s", dir, v->d_name) >= (int)sizeof p || ++n_profili > MAX_PROFILI_SFONDI) {
            esito = ph_errore(e, "troppi profili o percorso troppo lungo");
            break;
        }
        PhBuffer b = PH_BUFFER_VUOTO;
        int r = ph_cfg_leggi_file(p, PH_CFG_MAX_FILE, &b, e);
        if (r == 1) { /* sparito nel frattempo: non c'e' piu' niente da leggere */
            ph_buffer_libera(&b);
            continue;
        }
        if (r != 0 || ph_cfg_telefono_da_testo((const char *)b.dati, b.lunghezza, t, e) < 0) {
            ph_errore_contesto(e, "profilo %s illeggibile, nessuno sfondo cancellato", v->d_name);
            esito = -2;
        } else if (t->sfondo.immagine[0] != '\0' && ph_cfg_nome_cache_valido(t->sfondo.immagine) &&
                   n_rif < MAX_PROFILI_SFONDI) {
            memcpy(rif[n_rif++], t->sfondo.immagine, strlen(t->sfondo.immagine) + 1);
        }
        ph_buffer_libera(&b);
    }
    if (esito == 0 && errno != 0) /* errore di lettura, non fine dell'elenco: nessun profilo e' sicuro */
        esito = ph_errore(e, "errore leggendo la cartella %s: %s", dir, strerror(errno));
    closedir(dc);
    ph_cfg_telefono_libera(t);
    if (esito != 0) {
        free(rif);
        return esito;
    }

    int cfd = open(cache, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (cfd < 0) {
        free(rif);
        if (errno == ENOENT)
            return 0;
        return ph_errore(e, "impossibile aprire %s: %s", cache, strerror(errno));
    }
    struct stat sc;
    if (fstat(cfd, &sc) < 0 || sc.st_uid != geteuid()) {
        close(cfd);
        free(rif);
        return ph_errore(e, "la cartella %s non e' dell'utente", cache);
    }
    DIR *dd = fdopendir(cfd); /* prende possesso di cfd */
    if (dd == NULL) {
        close(cfd);
        free(rif);
        return ph_errore(e, "impossibile leggere %s", cache);
    }
    time_t ora = time(NULL);
    int tolti = 0;
    while ((v = leggi_voce(dd)) != NULL) {
        if (!nome_sfondo(v->d_name))
            continue;
        bool riferito = false;
        for (size_t i = 0; i < n_rif; i++)
            if (strcmp(rif[i], v->d_name) == 0)
                riferito = true;
        struct stat st;
        if (riferito || fstatat(cfd, v->d_name, &st, AT_SYMLINK_NOFOLLOW) < 0 || !S_ISREG(st.st_mode) ||
            st.st_uid != geteuid() || ora - st.st_mtime < ETA_MINIMA_SFONDO)
            continue;
        if (unlinkat(cfd, v->d_name, 0) == 0)
            tolti++;
    }
    int err = errno; /* di leggi_voce: 0 alla fine regolare dell'elenco */
    closedir(dd);
    free(rif);
    if (err != 0)
        return ph_errore(e, "errore leggendo la cartella %s: %s (file tolti prima: %d)", cache, strerror(err), tolti);
    return tolti;
}
