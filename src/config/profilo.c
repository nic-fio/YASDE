/* Profilo per telefono (vedi profilo.h). */
#include "config/profilo.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "config/config.h"
#include "config/config_interno.h"
#include "config/toml.h"

/* ---------- validazione ---------- */

static bool alnum(unsigned char c)
{
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

static bool nome_valido(const char *s)
{
    size_t n = strlen(s);
    if (n == 0 || n >= PH_PROF_LUN_NOME)
        return false;
    for (size_t i = 0; i < n; i++)
        if (!alnum((unsigned char)s[i]) && s[i] != '_' && s[i] != '.' && s[i] != '-')
            return false;
    return true;
}

/* Modello e codificatori finiscono tali e quali nella tabella pubblica. */
static bool testo_tabella_valido(const char *s, size_t cap)
{
    size_t n = strlen(s);
    if (n >= cap)
        return false;
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        if (!alnum(c) && strchr(" ._+()/,-", c) == NULL)
            return false;
    }
    return true;
}

static bool data_valida(const char *s)
{
    if (strlen(s) != 10)
        return false;
    for (int i = 0; i < 10; i++) {
        if (i == 4 || i == 7) {
            if (s[i] != '-')
                return false;
        } else if (s[i] < '0' || s[i] > '9') {
            return false;
        }
    }
    int anno = atoi(s), mese = atoi(s + 5), giorno = atoi(s + 8);
    if (anno < 2000 || anno > 2999 || mese < 1 || mese > 12 || giorno < 1)
        return false;
    static const int giorni[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    int massimo = giorni[mese - 1];
    if (mese == 2 && (anno % 4 == 0 && (anno % 100 != 0 || anno % 400 == 0)))
        massimo = 29;
    return giorno <= massimo;
}

/* ---------- struttura ---------- */

void ph_profilo_vuoto(PhProfilo *p)
{
    memset(p, 0, sizeof *p);
}

PhProfilo *ph_profilo_nuovo(void)
{
    PhProfilo *p = malloc(sizeof *p);
    if (p != NULL)
        ph_profilo_vuoto(p);
    return p;
}

void ph_profilo_libera(PhProfilo *p)
{
    free(p);
}

/* ---------- lettura ---------- */

static void leggi_voci(const PhToml *doc, const char *tabella, PhProfVoce *v, size_t *n)
{
    for (size_t i = 1; i < doc->n && *n < PH_PROF_MAX_VOCI; i++) {
        const PhTomlTabella *t = &doc->tabelle[i];
        if (!t->ripetuta || strcmp(t->nome, tabella) != 0)
            continue;
        const PhTomlVoce *nome = ph_toml_voce(t, "nome", PH_TOML_STRINGA);
        if (nome == NULL || !nome_valido(nome->s))
            continue;
        bool doppio = false;
        for (size_t k = 0; k < *n; k++)
            if (strcmp(v[k].nome, nome->s) == 0)
                doppio = true;
        if (doppio)
            continue;
        PhProfVoce x;
        memset(&x, 0, sizeof x);
        memcpy(x.nome, nome->s, strlen(nome->s) + 1);
        const PhTomlVoce *nota = ph_toml_voce(t, "nota", PH_TOML_STRINGA);
        if (nota != NULL && strlen(nota->s) < sizeof x.nota)
            memcpy(x.nota, nota->s, strlen(nota->s) + 1);
        const PhTomlVoce *data = ph_toml_voce(t, "data", PH_TOML_STRINGA);
        if (data != NULL && data_valida(data->s))
            memcpy(x.data, data->s, sizeof x.data);
        v[(*n)++] = x;
    }
}

static const PhTomlTabella *tabella(const PhToml *doc, const char *nome)
{
    for (size_t i = 1; i < doc->n; i++)
        if (!doc->tabelle[i].ripetuta && strcmp(doc->tabelle[i].nome, nome) == 0)
            return &doc->tabelle[i];
    return NULL;
}

static void leggi_testo_tab(const PhTomlTabella *t, const char *k, char *dst, size_t cap)
{
    const PhTomlVoce *v = ph_toml_voce(t, k, PH_TOML_STRINGA);
    if (v != NULL && testo_tabella_valido(v->s, cap))
        memcpy(dst, v->s, strlen(v->s) + 1);
}

static int leggi_int(const PhTomlTabella *t, const char *k, long min, long max, int *dst)
{
    const PhTomlVoce *v = ph_toml_voce(t, k, PH_TOML_INTERO);
    if (v == NULL || v->i < min || v->i > max)
        return 0;
    *dst = (int)v->i;
    return 1;
}

/* *futuro = true se il file e' di versione futura (errore da non "curare"
 * mettendo il file da parte). */
static int da_testo_int(const char *testo, size_t lunghezza, PhProfilo *p, bool *futuro, PhErrore *e)
{
    *futuro = false;
    ph_profilo_vuoto(p);
    PhToml doc;
    if (ph_toml_leggi(testo, lunghezza, &doc, e) < 0)
        return -1;
    const PhTomlTabella *t = tabella(&doc, "profilo");
    if (t != NULL) {
        const PhTomlVoce *ver = ph_toml_voce(t, "versione", PH_TOML_INTERO);
        if (ver != NULL && ver->i > PH_PROF_VERSIONE) {
            long long v = ver->i;
            ph_toml_libera(&doc);
            *futuro = true;
            return ph_errore(e, "profilo di versione %lld, questa versione conosce solo la %d", v, PH_PROF_VERSIONE);
        }
        leggi_testo_tab(t, "modello", p->modello, sizeof p->modello);
        leggi_int(t, "api", 1, 999, &p->api);
    }
    if ((t = tabella(&doc, "encoder")) != NULL) {
        leggi_testo_tab(t, "scelto", p->encoder, sizeof p->encoder);
        leggi_testo_tab(t, "ripiego", p->ripiego, sizeof p->ripiego);
        const PhTomlVoce *r = ph_toml_voce(t, "repeat_previous_frame_after_ignorato", PH_TOML_BOOLEANO);
        if (r != NULL)
            p->repeat_ignorato = r->b ? PH_PROF_SI : PH_PROF_NO;
    }
    if ((t = tabella(&doc, "misure")) != NULL)
        leggi_int(t, "max_flussi", 1, PH_PROF_MAX_FLUSSI, &p->max_flussi);
    leggi_voci(&doc, "sintomo", p->sintomi, &p->n_sintomi);
    leggi_voci(&doc, "workaround", p->workaround, &p->n_workaround);
    ph_toml_libera(&doc);
    return 0;
}

int ph_profilo_da_testo(const char *testo, size_t lunghezza, PhProfilo *p, PhErrore *e)
{
    bool futuro;
    return da_testo_int(testo, lunghezza, p, &futuro, e);
}

/* ---------- scrittura ---------- */

#define SCRIVI(chiamata) \
    do {                 \
        if ((chiamata) < 0) \
            return -1;   \
    } while (0)

static int scrivi_voci(PhBuffer *out, const char *tabella_nome, const PhProfVoce *v, size_t n)
{
    for (size_t i = 0; i < n && i < PH_PROF_MAX_VOCI; i++) {
        SCRIVI(ph_toml_scrivi_tabella(out, tabella_nome, true));
        SCRIVI(ph_toml_scrivi_stringa(out, "nome", v[i].nome));
        if (v[i].nota[0] != '\0')
            SCRIVI(ph_toml_scrivi_stringa(out, "nota", v[i].nota));
        if (v[i].data[0] != '\0')
            SCRIVI(ph_toml_scrivi_stringa(out, "data", v[i].data));
    }
    return 0;
}

int ph_profilo_in_testo(const PhProfilo *p, PhBuffer *out)
{
    SCRIVI(ph_toml_scrivi_commento(out, "Profilo di un telefono: cio' che e' stato misurato o osservato.\n"
                                        "Una chiave assente vuol dire \"non misurato\"."));
    SCRIVI(ph_toml_scrivi_tabella(out, "profilo", false));
    SCRIVI(ph_toml_scrivi_intero(out, "versione", PH_PROF_VERSIONE));
    if (p->modello[0] != '\0')
        SCRIVI(ph_toml_scrivi_stringa(out, "modello", p->modello));
    if (p->api != 0)
        SCRIVI(ph_toml_scrivi_intero(out, "api", p->api));
    SCRIVI(ph_toml_scrivi_tabella(out, "encoder", false));
    if (p->encoder[0] != '\0')
        SCRIVI(ph_toml_scrivi_stringa(out, "scelto", p->encoder));
    if (p->ripiego[0] != '\0')
        SCRIVI(ph_toml_scrivi_stringa(out, "ripiego", p->ripiego));
    if (p->repeat_ignorato != PH_PROF_NON_MISURATO)
        SCRIVI(ph_toml_scrivi_booleano(out, "repeat_previous_frame_after_ignorato", p->repeat_ignorato == PH_PROF_SI));
    SCRIVI(ph_toml_scrivi_tabella(out, "misure", false));
    if (p->max_flussi != 0)
        SCRIVI(ph_toml_scrivi_intero(out, "max_flussi", p->max_flussi));
    SCRIVI(scrivi_voci(out, "sintomo", p->sintomi, p->n_sintomi));
    SCRIVI(scrivi_voci(out, "workaround", p->workaround, p->n_workaround));
    return 0;
}

/* ---------- disco ---------- */

static int percorso_profilo(const char *seriale, char *out, size_t cap, PhErrore *e)
{
    char pulito[PH_CFG_LUN_SERIALE_FILE], dir[PATH_MAX];
    if (ph_cfg_seriale_pulito(seriale, pulito, sizeof pulito, e) < 0 || ph_cfg_cartella_config(dir, sizeof dir, e) < 0)
        return -1;
    int n = snprintf(out, cap, "%s/profilo-%s.toml", dir, pulito);
    if (n < 0 || (size_t)n >= cap)
        return ph_errore(e, "percorso del profilo troppo lungo");
    return 0;
}

static int dt_prof(const char *t, size_t n, void *d, PhErrore *e)
{
    bool futuro;
    int r = da_testo_int(t, n, d, &futuro, e);
    return r < 0 && futuro ? -2 : r; /* versione futura: non si tocca il file */
}
static void pr_prof(void *d) { ph_profilo_vuoto(d); }

int ph_profilo_carica(const char *seriale, PhProfilo *p, PhErrore *e)
{
    ph_profilo_vuoto(p);
    char percorso[PATH_MAX];
    if (percorso_profilo(seriale, percorso, sizeof percorso, e) < 0)
        return -1;
    return ph_cfg_carica_file(percorso, "profilo del telefono", PH_PROF_MAX_FILE, dt_prof, p, pr_prof, e);
}

int ph_profilo_salva(const char *seriale, const PhProfilo *p, PhErrore *e)
{
    char percorso[PATH_MAX];
    if (percorso_profilo(seriale, percorso, sizeof percorso, e) < 0)
        return -1;
    PhBuffer b = PH_BUFFER_VUOTO;
    int r = ph_profilo_in_testo(p, &b);
    if (r < 0)
        ph_errore(e, "memoria esaurita preparando il profilo");
    else
        r = ph_cfg_scrivi_file(percorso, b.dati, b.lunghezza, e);
    ph_buffer_libera(&b);
    return r;
}

/* ---------- aggiornamenti ---------- */

int ph_profilo_imposta_telefono(PhProfilo *p, const char *modello, int api)
{
    if (modello == NULL || !testo_tabella_valido(modello, sizeof p->modello) || api < 0 || api > 999)
        return -1;
    memcpy(p->modello, modello, strlen(modello) + 1);
    p->api = api;
    return 0;
}

int ph_profilo_imposta_encoder(PhProfilo *p, const char *scelto, const char *ripiego)
{
    if (ripiego == NULL)
        ripiego = "";
    if (scelto == NULL || !testo_tabella_valido(scelto, sizeof p->encoder) ||
        !testo_tabella_valido(ripiego, sizeof p->ripiego))
        return -1;
    memcpy(p->encoder, scelto, strlen(scelto) + 1);
    memcpy(p->ripiego, ripiego, strlen(ripiego) + 1);
    return 0;
}

void ph_profilo_imposta_repeat(PhProfilo *p, PhProfTri ignorato)
{
    p->repeat_ignorato = (ignorato == PH_PROF_NO || ignorato == PH_PROF_SI) ? ignorato : PH_PROF_NON_MISURATO;
}

int ph_profilo_imposta_max_flussi(PhProfilo *p, int flussi)
{
    if (flussi < 0 || flussi > PH_PROF_MAX_FLUSSI)
        return -1;
    p->max_flussi = flussi;
    return 0;
}

static int aggiungi(PhProfVoce *v, size_t *n, const char *nome, const char *nota, const char *data, PhErrore *e)
{
    if (nome == NULL || !nome_valido(nome))
        return ph_errore(e, "nome breve non valido (solo lettere, cifre, '_', '.', '-', al massimo %d caratteri)",
                         PH_PROF_LUN_NOME - 1);
    if (nota == NULL)
        nota = "";
    if (strlen(nota) >= PH_PROF_LUN_NOTA)
        return ph_errore(e, "nota troppo lunga (al massimo %d byte)", PH_PROF_LUN_NOTA - 1);
    char oggi[PH_PROF_LUN_DATA];
    if (data == NULL) {
        time_t t = time(NULL);
        struct tm tm;
        if (gmtime_r(&t, &tm) == NULL || strftime(oggi, sizeof oggi, "%Y-%m-%d", &tm) != 10)
            return ph_errore(e, "impossibile leggere la data di oggi");
        data = oggi;
    }
    if (!data_valida(data))
        return ph_errore(e, "data non valida (serve AAAA-MM-GG)");
    for (size_t i = 0; i < *n; i++) {
        if (strcmp(v[i].nome, nome) == 0) {
            memcpy(v[i].nota, nota, strlen(nota) + 1);
            memcpy(v[i].data, data, PH_PROF_LUN_DATA);
            return 1;
        }
    }
    if (*n >= PH_PROF_MAX_VOCI)
        return ph_errore(e, "troppe voci (al massimo %d)", PH_PROF_MAX_VOCI);
    memcpy(v[*n].nome, nome, strlen(nome) + 1);
    memcpy(v[*n].nota, nota, strlen(nota) + 1);
    memcpy(v[*n].data, data, PH_PROF_LUN_DATA);
    (*n)++;
    return 0;
}

int ph_profilo_aggiungi_sintomo(PhProfilo *p, const char *nome, const char *nota, const char *data, PhErrore *e)
{
    return aggiungi(p->sintomi, &p->n_sintomi, nome, nota, data, e);
}

int ph_profilo_aggiungi_workaround(PhProfilo *p, const char *nome, const char *nota, const char *data, PhErrore *e)
{
    return aggiungi(p->workaround, &p->n_workaround, nome, nota, data, e);
}

/* ---------- tabella dei telefoni testati ---------- */

const char *ph_profilo_tabella_intestazione(void)
{
    return "| Modello | Android | Codificatore | Ripiego | repeat-previous-frame-after | Flussi paralleli | Sintomi | "
           "Workaround |\n|---|---|---|---|---|---|---|---|\n";
}

typedef struct {
    char *out;
    size_t cap, n;
    bool pieno;
} Riga;

static void aggiungi_testo(Riga *r, const char *s)
{
    size_t l = strlen(s);
    if (r->pieno || r->n + l >= r->cap) {
        r->pieno = true;
        return;
    }
    memcpy(r->out + r->n, s, l + 1);
    r->n += l;
}

static void aggiungi_nomi(Riga *r, const PhProfVoce *v, size_t n)
{
    if (n == 0) {
        aggiungi_testo(r, "nessuno registrato");
        return;
    }
    for (size_t i = 0; i < n && i < PH_PROF_MAX_VOCI; i++) {
        if (i > 0)
            aggiungi_testo(r, ", ");
        aggiungi_testo(r, v[i].nome);
    }
}

int ph_profilo_riga_tabella(const PhProfilo *p, char *out, size_t capacita, PhErrore *e)
{
    if (capacita == 0)
        return ph_errore(e, "buffer vuoto");
    out[0] = '\0';
    Riga r = {out, capacita, 0, false};
    char tmp[64];
    aggiungi_testo(&r, "| ");
    aggiungi_testo(&r, p->modello[0] != '\0' ? p->modello : "non misurato");
    aggiungi_testo(&r, " | ");
    if (p->api == 0) {
        aggiungi_testo(&r, "non misurato");
    } else {
        /* Android 14 = API 34, 15 = 35, 16 = 36; oltre, solo il livello API. */
        if (p->api >= 34 && p->api <= 36)
            snprintf(tmp, sizeof tmp, "%d (API %d)", p->api - 20, p->api);
        else
            snprintf(tmp, sizeof tmp, "API %d", p->api);
        aggiungi_testo(&r, tmp);
    }
    aggiungi_testo(&r, " | ");
    aggiungi_testo(&r, p->encoder[0] != '\0' ? p->encoder : "non misurato");
    aggiungi_testo(&r, " | ");
    aggiungi_testo(&r, p->ripiego[0] != '\0' ? p->ripiego : (p->encoder[0] != '\0' ? "nessuno" : "non misurato"));
    aggiungi_testo(&r, " | ");
    aggiungi_testo(&r, p->repeat_ignorato == PH_PROF_SI   ? "ignorato (serve il ridisegno forzato)"
                       : p->repeat_ignorato == PH_PROF_NO ? "rispettato"
                                                          : "non misurato");
    aggiungi_testo(&r, " | ");
    if (p->max_flussi == 0) {
        aggiungi_testo(&r, "non misurato");
    } else {
        snprintf(tmp, sizeof tmp, "%d", p->max_flussi);
        aggiungi_testo(&r, tmp);
    }
    aggiungi_testo(&r, " | ");
    aggiungi_nomi(&r, p->sintomi, p->n_sintomi);
    aggiungi_testo(&r, " | ");
    aggiungi_nomi(&r, p->workaround, p->n_workaround);
    aggiungi_testo(&r, " |");
    if (r.pieno) {
        out[0] = '\0';
        return ph_errore(e, "la riga della tabella non entra nel buffer");
    }
    return 0;
}
