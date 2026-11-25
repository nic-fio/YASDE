/* Logica pura di shell-sistema (F-06, F-07, F-08). Vedi logica.h. */
#include "shell/sistema/logica.h"

#include <stdio.h>
#include <string.h>

#include "config/lingua.h"

/* Oltre questa lunghezza un testo non viene nemmeno letto: un testo di megabyte
 * non deve costare tempo sul thread GTK. */
#define LIMITE_LETTURA 65536

/* ---- Testi ostili ------------------------------------------------------- */

static void togli_ultimo_carattere(GString *g)
{
    if (g->len == 0)
        return;
    const char *fine = g->str + g->len;
    g_string_truncate(g, (gsize)(g_utf8_prev_char(fine) - g->str));
}

void ph_sistema_testo_sicuro(const char *origine, size_t max_caratteri, char *out, size_t capacita)
{
    if (capacita == 0)
        return;
    out[0] = '\0';
    if (origine == NULL || max_caratteri == 0)
        return;

    /* UTF-8 non valido: i byte sbagliati diventano U+FFFD, mai letti fuori. */
    char *valido = g_utf8_make_valid(origine, (gssize)strnlen(origine, LIMITE_LETTURA));
    GString *g = g_string_sized_new(64);
    size_t n = 0;
    bool spazio_in_sospeso = false, troncato = false;

    for (const char *p = valido; *p != '\0'; p = g_utf8_next_char(p)) {
        gunichar c = g_utf8_get_char(p);
        if (g_unichar_isspace(c) || g_unichar_iscntrl(c)) {
            /* A capo, tabulazioni e simili: uno spazio, ma non in testa. */
            spazio_in_sospeso = n > 0;
            continue;
        }
        /* Solo i controlli di direzione del testo (possono rovesciare la riga e
         * far leggere un'altra cosa): U+202A-202E, U+2066-2069, U+200E/200F,
         * U+061C. Gli altri caratteri invisibili (congiuntori a larghezza zero,
         * selettori di variante) servono alle emoji composte e ad alcune
         * lingue: restano. */
        if ((c >= 0x202A && c <= 0x202E) || (c >= 0x2066 && c <= 0x2069) || c == 0x200E ||
            c == 0x200F || c == 0x061C)
            continue;
        size_t servono = 1 + (spazio_in_sospeso ? 1 : 0);
        if (n + servono > max_caratteri) {
            troncato = true;
            break;
        }
        if (spazio_in_sospeso) {
            g_string_append_c(g, ' ');
            n++;
            spazio_in_sospeso = false;
        }
        g_string_append_unichar(g, c);
        n++;
    }
    g_free(valido);

    if (troncato) {
        /* "…" conta come un carattere: si fa posto togliendo l'ultimo. */
        if (n >= max_caratteri)
            togli_ultimo_carattere(g);
        while (g->len > 0 && g->str[g->len - 1] == ' ')
            g_string_truncate(g, g->len - 1);
        g_string_append(g, "\xE2\x80\xA6");
    }

    /* Mai oltre il buffer, e mai a meta' di un carattere. */
    size_t len = g->len;
    if (len > capacita - 1) {
        len = capacita - 1;
        while (len > 0 && ((unsigned char)g->str[len] & 0xC0) == 0x80)
            len--;
    }
    memcpy(out, g->str, len);
    out[len] = '\0';
    g_string_free(g, TRUE);
}

/* ---- Notifiche nascoste ----------------------------------------------------- */

struct PhNascoste {
    GHashTable *tabella; /* char *chiave -> uint64_t *ora_ms */
};

PhNascoste *ph_nascoste_nuove(void)
{
    PhNascoste *n = g_new0(PhNascoste, 1);
    n->tabella = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    return n;
}

void ph_nascoste_libera(PhNascoste *n)
{
    if (n == NULL)
        return;
    g_hash_table_destroy(n->tabella);
    g_free(n);
}

size_t ph_nascoste_numero(const PhNascoste *n) { return g_hash_table_size(n->tabella); }

bool ph_nascoste_nascondi(PhNascoste *n, const char *chiave, uint64_t ora_ms)
{
    if (chiave == NULL)
        return false;
    uint64_t *vecchia = g_hash_table_lookup(n->tabella, chiave);
    if (vecchia != NULL && *vecchia == ora_ms)
        return false;
    uint64_t *nuova = g_new(uint64_t, 1);
    *nuova = ora_ms;
    g_hash_table_replace(n->tabella, g_strdup(chiave), nuova);
    return true;
}

bool ph_nascoste_e_nascosta(const PhNascoste *n, const char *chiave, uint64_t ora_ms)
{
    if (chiave == NULL)
        return false;
    const uint64_t *ora = g_hash_table_lookup(n->tabella, chiave);
    return ora != NULL && *ora == ora_ms;
}

bool ph_nascoste_nascondi_tutte(PhNascoste *n, const PhSrvNotifica *v, size_t quante)
{
    bool cambiato = false;
    for (size_t i = 0; i < quante; i++)
        if (ph_nascoste_nascondi(n, v[i].chiave, v[i].ora_ms))
            cambiato = true;
    return cambiato;
}

typedef struct {
    const PhSrvNotifica *v;
    size_t quante;
} Fotografia;

/* Resta nascosta solo se esiste ancora con la stessa ora. */
static gboolean da_dimenticare(gpointer chiave, gpointer valore, gpointer dati)
{
    const Fotografia *f = dati;
    const uint64_t *ora = valore;
    for (size_t i = 0; i < f->quante; i++)
        if (f->v[i].chiave != NULL && strcmp(f->v[i].chiave, chiave) == 0 && f->v[i].ora_ms == *ora)
            return FALSE;
    return TRUE;
}

bool ph_nascoste_sincronizza(PhNascoste *n, const PhSrvNotifica *v, size_t quante, bool leggibile)
{
    if (!leggibile)
        return false;
    Fotografia f = {v, quante};
    return g_hash_table_foreach_remove(n->tabella, da_dimenticare, &f) > 0;
}

size_t ph_nascoste_visibili(const PhNascoste *n, const PhSrvNotifica *v, size_t quante)
{
    size_t k = 0;
    for (size_t i = 0; i < quante; i++)
        if (!ph_nascoste_e_nascosta(n, v[i].chiave, v[i].ora_ms))
            k++;
    return k;
}

/* ---- Notifiche nuove ------------------------------------------------------------ */

struct PhViste {
    GHashTable *chiavi; /* char *chiave -> nulla */
    bool base;
    unsigned generazione; /* generazione dei servizi a cui appartiene l'insieme */
};

static GHashTable *insieme_nuovo(void) { return g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL); }

PhViste *ph_viste_nuove(void)
{
    PhViste *v = g_new0(PhViste, 1);
    v->chiavi = insieme_nuovo();
    return v;
}

void ph_viste_libera(PhViste *v)
{
    if (v == NULL)
        return;
    g_hash_table_destroy(v->chiavi);
    g_free(v);
}

void ph_viste_azzera(PhViste *v)
{
    g_hash_table_remove_all(v->chiavi);
    v->base = false;
    v->generazione = 0;
}

bool ph_viste_ha_base(const PhViste *v) { return v->base; }

size_t ph_viste_registra(PhViste *v, const PhSrvNotifica *notifiche, size_t quante, bool leggibile,
                         unsigned generazione, bool pronte, size_t *indice_toast)
{
    size_t migliore = SIZE_MAX, nuove = 0;
    if (indice_toast != NULL)
        *indice_toast = SIZE_MAX;
    /* Prima lettura del collegamento attuale non ancora arrivata: la fotografia
     * e' di prima (puo' essere vecchia) e non conta mai, nemmeno come base. */
    if (!pronte || generazione == 0)
        return 0;
    if (generazione != v->generazione) {
        /* Prima fotografia di un collegamento nuovo: e' la base, anche se
         * illeggibile (allora la base sara' la prima leggibile). */
        g_hash_table_remove_all(v->chiavi);
        v->base = false;
        v->generazione = generazione;
    }
    if (!leggibile)
        return 0;

    if (v->base) {
        for (size_t i = 0; i < quante; i++) {
            const char *k = notifiche[i].chiave != NULL ? notifiche[i].chiave : "";
            if (g_hash_table_contains(v->chiavi, k))
                continue;
            nuove++;
            if (migliore == SIZE_MAX || notifiche[i].ora_ms > notifiche[migliore].ora_ms)
                migliore = i;
        }
    }

    /* Le chiavi viste diventano quelle della fotografia: una notifica sparita
     * e poi tornata e' di nuovo nuova. */
    GHashTable *nuovo = insieme_nuovo();
    for (size_t i = 0; i < quante; i++)
        g_hash_table_add(nuovo, g_strdup(notifiche[i].chiave != NULL ? notifiche[i].chiave : ""));
    g_hash_table_destroy(v->chiavi);
    v->chiavi = nuovo;
    v->base = true;

    if (indice_toast != NULL)
        *indice_toast = migliore;
    return nuove;
}

/* ---- Riga di una notifica ----------------------------------------------------- */

void ph_sistema_ora_formatta(uint64_t ora_ms, GTimeZone *fuso, char *out, size_t capacita)
{
    if (capacita == 0)
        return;
    /* 0 = ora ignota; oltre l'anno 9999 GLib non ha una data. */
    uint64_t secondi = ora_ms / 1000;
    GDateTime *utc = (ora_ms == 0 || secondi > 253402300799ULL)
                         ? NULL
                         : g_date_time_new_from_unix_utc((gint64)secondi);
    GDateTime *loc = utc != NULL && fuso != NULL ? g_date_time_to_timezone(utc, fuso) : NULL;
    char *testo = loc != NULL ? g_date_time_format(loc, "%H:%M") : NULL;
    snprintf(out, capacita, "%s", testo != NULL ? testo : "--:--");
    g_free(testo);
    if (loc != NULL)
        g_date_time_unref(loc);
    if (utc != NULL)
        g_date_time_unref(utc);
}

void ph_sistema_ora_locale(uint64_t ora_ms, char *out, size_t capacita)
{
    GTimeZone *fuso = g_time_zone_new_local();
    ph_sistema_ora_formatta(ora_ms, fuso, out, capacita);
    g_time_zone_unref(fuso);
}

void ph_sistema_app_ora(const char *nome_app, uint64_t ora_ms, GTimeZone *fuso, char *out,
                        size_t capacita)
{
    if (capacita == 0)
        return;
    char app[256], ora[16];
    ph_sistema_testo_sicuro(nome_app, 60, app, sizeof app);
    ph_sistema_ora_formatta(ora_ms, fuso, ora, sizeof ora);
    if (ph_lingua_componi(out, capacita, ph_t("{app} · {ora}"), "app", app, "ora", ora,
                          (const char *)NULL) == (size_t)-1)
        snprintf(out, capacita, "%s", app);
}

/* ---- Stato del telefono ----------------------------------------------------------- */

int ph_sistema_versione_android(int api)
{
    if (api >= 34)
        return api - 20;
    switch (api) {
    case 33: return 13;
    case 32:
    case 31: return 12;
    case 30: return 11;
    case 29: return 10;
    default: return 0;
    }
}

int ph_sistema_batteria_limita(int percento)
{
    return percento < 0 ? 0 : (percento > 100 ? 100 : percento);
}

void ph_sistema_batteria_pieno(int percento, char *out, size_t capacita)
{
    if (capacita == 0)
        return;
    int p = ph_sistema_batteria_limita(percento);
    if (p == 0) {
        out[0] = '\0';
        return;
    }
    /* Decimi di pixel in interi: niente virgola mobile, niente separatore
     * decimale della lingua. */
    int fine = 40 + 11 * p / 10;
    snprintf(out, capacita, "M4 8H%d.%dV12H4z", fine / 10, fine % 10);
}

const char *ph_sistema_rete_testo(PhSrvRete rete)
{
    switch (rete) {
    case PH_SRV_RETE_WIFI: return ph_t("Wi-Fi connesso");
    case PH_SRV_RETE_CELLULARE: return ph_t("Rete cellulare");
    case PH_SRV_RETE_NESSUNA: break;
    }
    return ph_t("Nessuna rete");
}

void ph_sistema_riga_telefono(const PhSrvCollegamento *c, const PhSrvTelefono *t, char *out,
                              size_t capacita)
{
    if (capacita == 0)
        return;
    out[0] = '\0';
    if (!c->ha_telefono)
        return;
    char versione[16], percento[16];
    int v = ph_sistema_versione_android(c->api);
    if (v > 0)
        snprintf(versione, sizeof versione, "%d", v);
    else
        snprintf(versione, sizeof versione, "?");
    size_t r;
    if (!t->noto) {
        r = ph_lingua_componi(out, capacita, ph_t("Android {versione}"), "versione", versione,
                              (const char *)NULL);
    } else {
        snprintf(percento, sizeof percento, "%d", ph_sistema_batteria_limita(t->batteria));
        r = ph_lingua_componi(out, capacita, ph_t("Android {versione} · Batteria {percento}% · {rete}"),
                              "versione", versione, "percento", percento, "rete",
                              ph_sistema_rete_testo(t->rete), (const char *)NULL);
    }
    if (r == (size_t)-1)
        snprintf(out, capacita, "Android %s", versione);
}

void ph_pannello_annulla(PhPannelloAttesa *a) { a->in_attesa = false; }

void ph_pannello_richiedi(PhPannelloAttesa *a, bool acceso, uint64_t ora_ms)
{
    a->in_attesa = true;
    a->richiesto = acceso;
    a->scadenza_ms = ora_ms + PH_PANNELLO_ATTESA_MS;
}

PhPannelloEsito ph_pannello_controlla(PhPannelloAttesa *a, bool acceso_ora, uint64_t ora_ms)
{
    if (!a->in_attesa)
        return PH_PANNELLO_NIENTE;
    if (acceso_ora == a->richiesto) {
        a->in_attesa = false;
        return PH_PANNELLO_FATTO;
    }
    if (ora_ms >= a->scadenza_ms) {
        a->in_attesa = false;
        return PH_PANNELLO_RIFIUTATO;
    }
    return PH_PANNELLO_NIENTE;
}

/* ---- Volume ------------------------------------------------------------------------ */

int ph_sistema_volume_limita(int volume) { return volume < 0 ? 0 : (volume > 100 ? 100 : volume); }

bool ph_sistema_volume_muto_mostrato(int volume, bool muto)
{
    return muto || ph_sistema_volume_limita(volume) == 0;
}

void ph_sistema_volume_testo(int volume, bool muto, char *out, size_t capacita)
{
    if (capacita == 0)
        return;
    if (ph_sistema_volume_muto_mostrato(volume, muto)) {
        snprintf(out, capacita, "%s", ph_t("Muto"));
        return;
    }
    char p[16];
    snprintf(p, sizeof p, "%d", ph_sistema_volume_limita(volume));
    if (ph_lingua_componi(out, capacita, ph_t("{percento}%"), "percento", p, (const char *)NULL) ==
        (size_t)-1)
        snprintf(out, capacita, "%s%%", p);
}

void ph_sistema_volume_etichetta(int volume, bool muto, char *out, size_t capacita)
{
    if (capacita == 0)
        return;
    if (ph_sistema_volume_muto_mostrato(volume, muto)) {
        snprintf(out, capacita, "%s", ph_t("Volume: muto"));
        return;
    }
    char p[16];
    snprintf(p, sizeof p, "%d", ph_sistema_volume_limita(volume));
    if (ph_lingua_componi(out, capacita, ph_t("Volume: {percento}%"), "percento", p,
                          (const char *)NULL) == (size_t)-1)
        snprintf(out, capacita, "%s", p);
}

bool ph_sistema_volume_nel_profilo(PhCfgTelefono *profilo, int volume)
{
    int v = ph_sistema_volume_limita(volume);
    if (profilo->volume == v)
        return false;
    profilo->volume = v;
    return true;
}
