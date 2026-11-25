/* Logica pura del pezzo trasferimento (percorsi.h). */
#include "shell/trasferimento/percorsi.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "config/lingua.h"

/* ---- Percorsi ------------------------------------------------------------ */

static void fallito(char *out, size_t cap)
{
    if (cap >= 2) {
        out[0] = '/';
        out[1] = '\0';
    } else if (cap == 1) {
        out[0] = '\0';
    }
}

/* Carattere da non lasciar passare in un percorso digitato. */
static bool e_controllo(gunichar c)
{
    return c < 0x20 || (c >= 0x7f && c <= 0x9f);
}

static bool normalizza_comune(const char *grezzo, const char *base, bool telefono, char *out, size_t cap)
{
    if (cap == 0)
        return false;
    fallito(out, cap);
    if (grezzo == NULL)
        return false;
    const char *a = grezzo;
    while (*a != '\0' && g_ascii_isspace(*a))
        a++;
    const char *b = a + strlen(a);
    while (b > a && g_ascii_isspace(b[-1]))
        b--;
    size_t len = (size_t)(b - a);
    if (len > PH_TRF_PERCORSO_MAX)
        return false;
    char *tmp = g_strndup(a, len);
    if (!g_utf8_validate(tmp, -1, NULL)) {
        g_free(tmp);
        return false;
    }
    for (const char *p = tmp; *p != '\0'; p = g_utf8_next_char(p)) {
        if (e_controllo(g_utf8_get_char(p))) {
            g_free(tmp);
            return false;
        }
    }
    /* "/" iniziale. PC: "~" e' la cartella personale, un percorso relativo parte da `base`. */
    char *s;
    if (tmp[0] == '/')
        s = g_strdup(tmp);
    else if (tmp[0] == '\0')
        s = g_strdup("/"); /* vuoto = radice */
    else if (!telefono && (strcmp(tmp, "~") == 0 || g_str_has_prefix(tmp, "~/")))
        s = g_strconcat(g_get_home_dir(), tmp + 1, NULL);
    else if (!telefono && base != NULL && base[0] == '/')
        s = g_strconcat(base, "/", tmp, NULL);
    else
        s = g_strconcat("/", tmp, NULL);
    g_free(tmp);
    /* Pezzi: "" e "." si saltano, ".." toglie l'ultimo (mai sopra "/"). */
    char **pezzi = g_strsplit(s, "/", -1);
    g_free(s);
    GPtrArray *pila = g_ptr_array_new();
    for (size_t i = 0; pezzi[i] != NULL; i++) {
        if (pezzi[i][0] == '\0' || strcmp(pezzi[i], ".") == 0)
            continue;
        if (strcmp(pezzi[i], "..") == 0) {
            if (pila->len > 0)
                g_ptr_array_remove_index(pila, pila->len - 1);
            continue;
        }
        g_ptr_array_add(pila, pezzi[i]);
    }
    GString *r = g_string_new(NULL);
    guint da = 0;
    /* Telefono: "/sdcard" (anche "//sdcard") = memoria interna. */
    if (telefono && pila->len > 0 && g_ascii_strcasecmp(g_ptr_array_index(pila, 0), "sdcard") == 0) {
        g_string_append(r, PH_TRF_MEMORIA_INTERNA);
        da = 1;
    }
    for (guint i = da; i < pila->len; i++) {
        g_string_append_c(r, '/');
        g_string_append(r, g_ptr_array_index(pila, i));
    }
    if (r->len == 0)
        g_string_append_c(r, '/');
    bool ok = r->len < cap && r->len <= PH_TRF_PERCORSO_MAX;
    if (ok)
        memcpy(out, r->str, r->len + 1);
    g_string_free(r, TRUE);
    g_ptr_array_unref(pila);
    g_strfreev(pezzi);
    return ok;
}

bool ph_trf_normalizza(const char *grezzo, char *out, size_t cap)
{
    return normalizza_comune(grezzo, NULL, true, out, cap);
}

bool ph_trf_normalizza_pc(const char *grezzo, const char *base, char *out, size_t cap)
{
    return normalizza_comune(grezzo, base, false, out, cap);
}

bool ph_trf_e_radice(const char *percorso)
{
    return percorso != NULL && strcmp(percorso, "/") == 0;
}

bool ph_trf_su(const char *percorso, char *out, size_t cap)
{
    if (cap == 0)
        return false;
    fallito(out, cap);
    if (percorso == NULL || percorso[0] != '/')
        return false;
    size_t n = strlen(percorso);
    while (n > 1 && percorso[n - 1] == '/')
        n--;
    if (n <= 1)
        return false; /* gia' alla radice */
    size_t i = n;
    while (i > 1 && percorso[i - 1] != '/')
        i--;
    /* percorso[i-1] e' la "/" prima dell'ultimo pezzo */
    size_t lunghezza = i - 1;
    if (lunghezza == 0)
        return true; /* "/" gia' scritta da fallito() */
    if (lunghezza >= cap)
        return false;
    memcpy(out, percorso, lunghezza);
    out[lunghezza] = '\0';
    return true;
}

bool ph_trf_nome_valido(const char *nome)
{
    return nome != NULL && nome[0] != '\0' && strchr(nome, '/') == NULL && strcmp(nome, ".") != 0 &&
           strcmp(nome, "..") != 0;
}

bool ph_trf_unisci(const char *cartella, const char *nome, char *out, size_t cap)
{
    if (cap > 0)
        out[0] = '\0';
    if (cartella == NULL || cartella[0] == '\0' || !ph_trf_nome_valido(nome))
        return false;
    size_t c = strlen(cartella), n = strlen(nome);
    while (c > 1 && cartella[c - 1] == '/')
        c--;
    bool radice = c == 1 && cartella[0] == '/';
    size_t tot = (radice ? 0 : c) + 1 + n;
    if (tot >= cap || tot > PH_TRF_PERCORSO_MAX)
        return false;
    size_t k = 0;
    if (!radice) {
        memcpy(out, cartella, c);
        k = c;
    }
    out[k++] = '/';
    memcpy(out + k, nome, n);
    out[k + n] = '\0';
    return true;
}

static bool uguali_senza_maiuscole(const char *a, const char *b)
{
    if (!g_utf8_validate(a, -1, NULL) || !g_utf8_validate(b, -1, NULL))
        return strcmp(a, b) == 0;
    char *x = g_utf8_casefold(a, -1), *y = g_utf8_casefold(b, -1);
    bool r = strcmp(x, y) == 0;
    g_free(x);
    g_free(y);
    return r;
}

bool ph_trf_nome_libero(const char *base, const char *const *esistenti, size_t n, char *out,
                        size_t cap)
{
    if (cap > 0)
        out[0] = '\0';
    if (base == NULL || base[0] == '\0')
        return false;
    /* Con n nomi occupati un libero c'e' entro n + 1 tentativi. */
    for (size_t k = 1; k <= n + 1; k++) {
        char prova[PH_TRF_PERCORSO_MAX];
        if (k == 1)
            g_strlcpy(prova, base, sizeof prova);
        else
            g_snprintf(prova, sizeof prova, "%s %zu", base, k);
        bool preso = false;
        for (size_t i = 0; i < n && !preso; i++)
            preso = esistenti[i] != NULL && uguali_senza_maiuscole(esistenti[i], prova);
        if (!preso) {
            if (strlen(prova) >= cap)
                return false;
            memcpy(out, prova, strlen(prova) + 1);
            return true;
        }
    }
    return false;
}

/* ---- Nascosti e pagine ---------------------------------------------------- */

bool ph_trf_nascosto(const char *nome)
{
    return nome != NULL && nome[0] == '.';
}

bool ph_trf_percorso_nascosto(const char *percorso)
{
    if (percorso == NULL)
        return false;
    for (const char *p = percorso; *p != '\0'; p++) {
        if (*p == '/' && p[1] == '.')
            return true;
    }
    return percorso[0] == '.';
}

size_t ph_trf_pagina_visibili(size_t totale, size_t pagine)
{
    if (pagine == 0)
        return 0;
    if (pagine > totale / PH_TRF_PAGINA + 1)
        return totale;
    size_t v = pagine * PH_TRF_PAGINA;
    return v < totale ? v : totale;
}

size_t ph_trf_pagina_prossime(size_t totale, size_t visibili)
{
    if (visibili >= totale)
        return 0;
    size_t resto = totale - visibili;
    return resto < PH_TRF_PAGINA ? resto : PH_TRF_PAGINA;
}

/* ---- Testi ---------------------------------------------------------------- */

/* Controlli e caratteri invisibili o che cambiano la direzione del testo. */
static bool da_nascondere(gunichar c)
{
    return c < 0x20 || (c >= 0x7f && c <= 0x9f) || c == 0x061C || (c >= 0x200B && c <= 0x200F) ||
           c == 0x2028 || c == 0x2029 || (c >= 0x202A && c <= 0x202E) ||
           (c >= 0x2060 && c <= 0x206F) || c == 0xFEFF || (c >= 0xFFF9 && c <= 0xFFFB);
}

char *ph_trf_testo_sicuro(const char *grezzo, size_t max_byte)
{
    if (max_byte < 4)
        max_byte = 4;
    char *valido = g_utf8_make_valid(grezzo != NULL ? grezzo : "", -1);
    bool tagliare = strlen(valido) > max_byte;
    size_t limite = tagliare ? max_byte - 3 : max_byte;
    GString *r = g_string_new(NULL);
    for (const char *p = valido; *p != '\0'; p = g_utf8_next_char(p)) {
        gunichar c = g_utf8_get_char(p);
        char buf[8];
        int k = da_nascondere(c) ? g_unichar_to_utf8('?', buf) : g_unichar_to_utf8(c, buf);
        if (r->len + (size_t)k > limite && tagliare)
            break;
        g_string_append_len(r, buf, k);
    }
    if (tagliare)
        g_string_append(r, "…");
    g_free(valido);
    return g_string_free(r, FALSE);
}

/* Un decimale, con aritmetica intera: printf("%.1f") userebbe il separatore
 * della lingua del PC (virgola su un PC italiano) anche con l'app in inglese.
 * Il separatore lo decide solo la lingua dell'app. */
static void cifra_decimale(double v, char *num, size_t cap)
{
    unsigned long long dec = (unsigned long long)floor(v * 10.0 + 0.5);
    char sep = ph_lingua_attiva() == PH_LINGUA_IT ? ',' : '.';
    snprintf(num, cap, "%llu%c%llu", dec / 10, sep, dec % 10);
}

void ph_trf_dimensione(uint64_t byte, char *out, size_t cap)
{
    char num[64];
    if (byte >= (UINT64_C(1) << 30)) {
        cifra_decimale((double)byte / 1073741824.0, num, sizeof num);
        ph_lingua_componi(out, cap, ph_t("{numero} GB"), "numero", num, NULL);
    } else if (byte >= (UINT64_C(1) << 20)) {
        cifra_decimale((double)byte / 1048576.0, num, sizeof num);
        ph_lingua_componi(out, cap, ph_t("{numero} MB"), "numero", num, NULL);
    } else if (byte >= 1024) {
        snprintf(num, sizeof num, "%llu", (unsigned long long)((byte + 512) / 1024));
        ph_lingua_componi(out, cap, ph_t("{numero} KB"), "numero", num, NULL);
    } else {
        snprintf(num, sizeof num, "%llu", (unsigned long long)byte);
        ph_lingua_componi(out, cap, ph_t("{numero} B"), "numero", num, NULL);
    }
}

unsigned ph_trf_percento(uint64_t fatti, uint64_t dimensione)
{
    if (dimensione == 0)
        return 0;
    if (fatti >= dimensione)
        return 100;
    /* Intero e senza overflow; 100 solo a trasferimento finito. */
    uint64_t p = fatti <= UINT64_MAX / 100 ? fatti * 100 / dimensione : fatti / (dimensione / 100);
    return p > 99 ? 99u : (unsigned)p;
}

void ph_trf_stato_testo(PhSrvTrStato stato, unsigned percento, char *out, size_t cap)
{
    if (cap == 0)
        return;
    switch (stato) {
    case PH_SRV_TR_CORSO: {
        char num[16];
        snprintf(num, sizeof num, "%u", percento > 100 ? 100u : percento);
        ph_lingua_componi(out, cap, ph_t("{percento}%"), "percento", num, NULL);
        break;
    }
    case PH_SRV_TR_COMPLETATO:
        g_strlcpy(out, ph_t("Completato"), cap);
        break;
    case PH_SRV_TR_FALLITO:
        g_strlcpy(out, ph_t("Non riuscito"), cap);
        break;
    case PH_SRV_TR_ANNULLATO:
        g_strlcpy(out, ph_t("Annullato"), cap);
        break;
    case PH_SRV_TR_ATTESA:
    default:
        g_strlcpy(out, ph_t("In attesa"), cap);
        break;
    }
}

void ph_trf_meta_testo(const PhSrvTrasferimento *t, char *out, size_t cap)
{
    if (cap == 0)
        return;
    const char *grezzo = t->destinazione;
    if (t->stato == PH_SRV_TR_FALLITO && t->errore != NULL && t->errore[0] != '\0')
        grezzo = t->errore;
    char *dest = ph_trf_testo_sicuro(grezzo, 1024);
    if (t->dimensione == 0) {
        g_strlcpy(out, dest, cap);
    } else {
        char dim[64];
        ph_trf_dimensione(t->dimensione, dim, sizeof dim);
        ph_lingua_componi(out, cap, ph_t("{dimensione} · {destinazione}"), "dimensione", dim,
                          "destinazione", dest, NULL);
    }
    g_free(dest);
}

bool ph_trf_attivo(PhSrvTrStato stato)
{
    return stato == PH_SRV_TR_ATTESA || stato == PH_SRV_TR_CORSO;
}

/* ---- Completati tra due fotografie ----------------------------------------- */
struct PhTrfFoto {
    GHashTable *stati; /* id (guint64 *) -> stato + 1 */
};

PhTrfFoto *ph_trf_foto_nuova(void)
{
    PhTrfFoto *f = g_new0(PhTrfFoto, 1);
    f->stati = g_hash_table_new_full(g_int64_hash, g_int64_equal, g_free, NULL);
    return f;
}

void ph_trf_foto_libera(PhTrfFoto *f)
{
    if (f == NULL)
        return;
    g_hash_table_unref(f->stati);
    g_free(f);
}

static GArray *confronta_e_sostituisci(PhTrfFoto *f, const PhSrvTrasferimento *v, size_t n,
                                       bool dire)
{
    GArray *completati = g_array_new(FALSE, FALSE, sizeof(guint));
    GHashTable *nuova = g_hash_table_new_full(g_int64_hash, g_int64_equal, g_free, NULL);
    for (size_t i = 0; i < n; i++) {
        gint64 *k = g_new(gint64, 1);
        *k = (gint64)v[i].id;
        gpointer prima = NULL;
        bool c = g_hash_table_lookup_extended(f->stati, k, NULL, &prima);
        if (dire && v[i].stato == PH_SRV_TR_COMPLETATO &&
            (!c || GPOINTER_TO_INT(prima) != (int)PH_SRV_TR_COMPLETATO + 1)) {
            guint idx = (guint)i;
            g_array_append_val(completati, idx);
        }
        g_hash_table_replace(nuova, k, GINT_TO_POINTER((int)v[i].stato + 1));
    }
    g_hash_table_unref(f->stati);
    f->stati = nuova;
    return completati;
}

GArray *ph_trf_foto_completati(PhTrfFoto *f, const PhSrvTrasferimento *v, size_t n)
{
    return confronta_e_sostituisci(f, v, n, true);
}

void ph_trf_foto_riprendi(PhTrfFoto *f, const PhSrvTrasferimento *v, size_t n)
{
    g_array_free(confronta_e_sostituisci(f, v, n, false), TRUE);
}

/* ---- Quali voci della coda mostrare ----------------------------------------------------------------------- */
size_t ph_trf_scegli_voci(const PhSrvTrasferimento *v, size_t n, PhSrvVerso verso, size_t max, size_t *out,
                          size_t *attive_nascoste)
{
    size_t k = 0, attive = 0;
    /* Prima in corso, poi in attesa (nell'ordine della coda), poi le finite, la piu' recente in alto. */
    for (int fase = 0; fase < 2; fase++) {
        for (size_t i = 0; i < n; i++) {
            PhSrvTrStato st = v[i].stato;
            if (v[i].verso != verso || !ph_trf_attivo(st))
                continue;
            if ((fase == 0) != (st == PH_SRV_TR_CORSO))
                continue;
            attive++;
            if (k < max)
                out[k++] = i;
        }
    }
    if (attive_nascoste != NULL)
        *attive_nascoste = attive - k;
    for (size_t i = n; i > 0 && k < max; i--) {
        if (v[i - 1].verso == verso && !ph_trf_attivo(v[i - 1].stato))
            out[k++] = i - 1;
    }
    return k;
}
