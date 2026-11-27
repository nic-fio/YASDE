/* Logica pura di «Chiamate recenti», «Rete» e «In riproduzione»: vedi logica_nuovi.h. */
#include "logica_nuovi.h"

#include <string.h>

#include "logica_telefono.h"
#include "logica_vecchi.h"
#include "logica_wx.h"
#include "config/lingua.h"

const char *const PH_DSK_PACCHETTI_TELEFONO[] = {
    "com.google.android.dialer", "com.samsung.android.dialer", "com.android.dialer",
    "com.android.contacts", "com.sec.android.app.contacts"};
const size_t PH_DSK_PACCHETTI_TELEFONO_N = sizeof PH_DSK_PACCHETTI_TELEFONO / sizeof PH_DSK_PACCHETTI_TELEFONO[0];

static void vuoto(char *out, size_t cap)
{
    if (out != NULL && cap > 0)
        out[0] = '\0';
}

static void copia(char *out, size_t cap, const char *t)
{
    if (out == NULL || cap == 0)
        return;
    g_strlcpy(out, t != NULL ? t : "", cap);
}

/* Copia un campo di dimensione fissa arrivato dal telefono, che puo' NON essere chiuso da NUL:
 * si legge al massimo `n_campo` byte (g_strlcpy scorrerebbe la sorgente fino al NUL per darne la
 * lunghezza, e leggerebbe oltre il campo). `dest` e' sempre chiuso (cap >= 1). */
void ph_dsk_copia_campo(char *dest, size_t cap, const char *campo, size_t n_campo)
{
    if (dest == NULL || cap == 0)
        return;
    size_t n = campo != NULL ? strnlen(campo, n_campo) : 0;
    if (n > cap - 1)
        n = cap - 1;
    if (n > 0)
        memcpy(dest, campo, n);
    dest[n] = '\0';
}

/* ---- Chiamate -------------------------------------------------------------------------------- */
size_t ph_dsk_chiamate_righe_max(int larghezza, int altezza)
{
    /* Stessa riga delle notifiche (tondo, titolo, sottotitolo, ora). */
    return ph_dsk_notifiche_righe_max(larghezza, altezza);
}

size_t ph_dsk_chiamate_da_mostrare(const PhSrvChiamate *c, int larghezza, int altezza)
{
    if (c == NULL || c->stato != PH_SRV_LET_OK)
        return 0;
    size_t n = c->n < PH_SRV_CHIAMATE_MAX ? c->n : PH_SRV_CHIAMATE_MAX; /* dato ostile */
    size_t max = ph_dsk_chiamate_righe_max(larghezza, altezza);
    return n < max ? n : max;
}

void ph_dsk_chiamate_stato_testo(const PhSrvChiamate *c, char *out, size_t cap)
{
    vuoto(out, cap);
    if (c == NULL)
        return;
    if (c->stato == PH_SRV_LET_NON_DISPONIBILE || c->stato == PH_SRV_LET_NEGATO)
        copia(out, cap, ph_t("Non disponibile su questo telefono"));
    else if (c->stato == PH_SRV_LET_OK && c->n == 0)
        copia(out, cap, ph_t("Nessuna chiamata recente"));
}

int ph_dsk_chiamata_tipo_valido(uint8_t tipo)
{
    return tipo >= PH_SRV_CHIAMATA_ENTRANTE && tipo <= PH_SRV_CHIAMATA_RIFIUTATA ? tipo : PH_SRV_CHIAMATA_ALTRO;
}

const char *ph_dsk_chiamata_tipo_testo(uint8_t tipo)
{
    switch (ph_dsk_chiamata_tipo_valido(tipo)) {
    case PH_SRV_CHIAMATA_ENTRANTE: return ph_t("Entrante");
    case PH_SRV_CHIAMATA_USCENTE: return ph_t("Uscente");
    case PH_SRV_CHIAMATA_PERSA: return ph_t("Persa");
    case PH_SRV_CHIAMATA_RIFIUTATA: return ph_t("Rifiutata");
    default: return "";
    }
}

void ph_dsk_chiamata_titolo(const PhSrvChiamata *c, char *out, size_t cap)
{
    vuoto(out, cap);
    if (c == NULL || out == NULL || cap == 0)
        return;
    char t[256];
    /* I campi arrivano da un buffer di dimensione fissa e possono non essere chiusi: copia limitata. */
    char nome[sizeof c->nome], numero[sizeof c->numero];
    ph_dsk_copia_campo(nome, sizeof nome, c->nome, sizeof c->nome);
    ph_dsk_copia_campo(numero, sizeof numero, c->numero, sizeof c->numero);
    ph_dsk_nome_sicuro(nome, t, sizeof t);
    if (t[0] == '\0')
        ph_dsk_nome_sicuro(numero, t, sizeof t);
    if (t[0] == '\0')
        g_strlcpy(t, ph_t("Numero nascosto"), sizeof t);
    g_strlcpy(out, t, cap);
}

void ph_dsk_chiamata_ora(uint64_t ora_ms, uint64_t adesso_ms, GTimeZone *tz, char *out, size_t cap)
{
    vuoto(out, cap);
    if (out == NULL || cap == 0 || ora_ms == 0 || ora_ms > 253402300799000ULL || adesso_ms > 253402300799000ULL)
        return; /* ora non nota o assurda (oltre l'anno 9999): non si inventa */
    int64_t g = ph_dsk_giorno_locale(tz, ora_ms);
    int64_t oggi = ph_dsk_giorno_locale(tz, adesso_ms);
    if (g == oggi)
        ph_dsk_sve_ora_testo(ora_ms, tz, out, cap);
    else
        ph_dsk_giorno_etichetta(g, oggi, out, cap);
}

PhDskAccento ph_dsk_chiamata_accento(uint8_t tipo)
{
    return ph_dsk_chiamata_tipo_valido(tipo) == PH_SRV_CHIAMATA_PERSA ? PH_DSK_ACC_CORALLO : PH_DSK_ACC_ARDESIA;
}

/* ---- Rete ------------------------------------------------------------------------------------ */
int ph_dsk_livello_valido(int livello)
{
    return livello < 0 ? 0 : (livello > 4 ? 4 : livello);
}

static bool rete_letta(const PhSrvReteDettaglio *r)
{
    return r != NULL && r->stato == PH_SRV_LET_OK;
}

void ph_dsk_rete_stato_testo(const PhSrvReteDettaglio *r, char *out, size_t cap)
{
    vuoto(out, cap);
    if (r != NULL && (r->stato == PH_SRV_LET_NON_DISPONIBILE || r->stato == PH_SRV_LET_NEGATO))
        copia(out, cap, ph_t("Non disponibile su questo telefono"));
}

void ph_dsk_rete_wifi(const PhSrvReteDettaglio *r, int *livello, char *testo, size_t cap)
{
    if (livello != NULL)
        *livello = 0;
    vuoto(testo, cap);
    if (!rete_letta(r))
        return;
    if (r->wifi == PH_SRV_WIFI_CONNESSO) {
        char nome[sizeof r->wifi_nome], sicuro[256];
        ph_dsk_copia_campo(nome, sizeof nome, r->wifi_nome, sizeof r->wifi_nome);
        ph_dsk_nome_sicuro(nome, sicuro, sizeof sicuro);
        copia(testo, cap, sicuro[0] != '\0' ? sicuro : ph_t("Connesso"));
        if (livello != NULL)
            *livello = ph_dsk_livello_valido(r->wifi_livello);
    } else if (r->wifi == PH_SRV_WIFI_ACCESO) {
        copia(testo, cap, ph_t("Non connesso"));
    } else {
        copia(testo, cap, ph_t("Spento"));
    }
}

const char *ph_dsk_rete_generazione(uint8_t g)
{
    switch (g) {
    case 2: return "2G";
    case 3: return "3G";
    case 4: return "4G";
    case 5: return "5G";
    default: return "";
    }
}

void ph_dsk_rete_mobile(const PhSrvReteDettaglio *r, int *livello, char *testo, size_t cap, char *nota, size_t cap_nota)
{
    if (livello != NULL)
        *livello = 0;
    vuoto(testo, cap);
    vuoto(nota, cap_nota);
    if (!rete_letta(r))
        return;
    if (r->mobile == PH_SRV_MOBILE_IN_SERVIZIO) {
        char op[sizeof r->operatore], sicuro[160];
        ph_dsk_copia_campo(op, sizeof op, r->operatore, sizeof r->operatore);
        ph_dsk_nome_sicuro(op, sicuro, sizeof sicuro);
        const char *gen = ph_dsk_rete_generazione(r->generazione);
        char riga[320];
        if (sicuro[0] != '\0' && gen[0] != '\0')
            g_snprintf(riga, sizeof riga, "%s \xc2\xb7 %s", sicuro, gen); /* il punto di mezzo */
        else if (sicuro[0] != '\0')
            g_strlcpy(riga, sicuro, sizeof riga);
        else
            g_strlcpy(riga, gen[0] != '\0' ? gen : ph_t("Connesso"), sizeof riga);
        copia(testo, cap, riga);
        if (livello != NULL)
            *livello = ph_dsk_livello_valido(r->mobile_livello);
        if (r->dati_mobili == 0) /* 0 no, 1 si', 2 ignoto: con «ignoto» la riga non c'e' */
            copia(nota, cap_nota, ph_t("Dati mobili spenti"));
    } else if (r->mobile == PH_SRV_MOBILE_SENZA_SERVIZIO) {
        copia(testo, cap, ph_t("Nessun servizio"));
    } else {
        copia(testo, cap, ph_t("Nessuna SIM"));
    }
}

bool ph_dsk_rete_aereo(const PhSrvReteDettaglio *r)
{
    return rete_letta(r) && r->aereo == 1; /* 0 no, 1 si', 2 ignoto: niente scritta */
}

/* ---- In riproduzione ---------------------------------------------------------------------------- */
bool ph_dsk_media_attivo(const PhSrvMedia *m)
{
    return m != NULL && m->stato_lettura == PH_SRV_LET_OK &&
           (m->stato == PH_SRV_MEDIA_PAUSA || m->stato == PH_SRV_MEDIA_RIPRODUZIONE);
}

bool ph_dsk_media_suona(const PhSrvMedia *m)
{
    return ph_dsk_media_attivo(m) && m->stato == PH_SRV_MEDIA_RIPRODUZIONE;
}

void ph_dsk_media_stato_testo(const PhSrvMedia *m, char *out, size_t cap)
{
    vuoto(out, cap);
    if (m == NULL)
        return;
    if (m->stato_lettura == PH_SRV_LET_NON_DISPONIBILE || m->stato_lettura == PH_SRV_LET_NEGATO)
        copia(out, cap, ph_t("Non disponibile su questo telefono"));
    else if (m->stato_lettura == PH_SRV_LET_OK && !ph_dsk_media_attivo(m))
        copia(out, cap, ph_t("Niente in riproduzione"));
}

void ph_dsk_media_titolo(const PhSrvMedia *m, char *out, size_t cap)
{
    vuoto(out, cap);
    if (m == NULL)
        return;
    char t[sizeof m->titolo], sicuro[256];
    ph_dsk_copia_campo(t, sizeof t, m->titolo, sizeof m->titolo);
    ph_dsk_nome_sicuro(t, sicuro, sizeof sicuro);
    copia(out, cap, sicuro[0] != '\0' ? sicuro : ph_t("Senza titolo"));
}

void ph_dsk_media_artista(const PhSrvMedia *m, char *out, size_t cap)
{
    vuoto(out, cap);
    if (m == NULL)
        return;
    char t[sizeof m->artista], sicuro[256];
    ph_dsk_copia_campo(t, sizeof t, m->artista, sizeof m->artista);
    ph_dsk_nome_sicuro(t, sicuro, sizeof sicuro);
    copia(out, cap, sicuro);
}

bool ph_dsk_media_puo(const PhSrvMedia *m, int comando)
{
    if (!ph_dsk_media_attivo(m))
        return false;
    switch (comando) {
    case PH_SRV_MEDIA_CMD_PLAY_PAUSA: return (m->comandi & PH_SRV_MEDIA_PUO_PLAY_PAUSA) != 0;
    case PH_SRV_MEDIA_CMD_AVANTI: return (m->comandi & PH_SRV_MEDIA_PUO_AVANTI) != 0;
    case PH_SRV_MEDIA_CMD_INDIETRO: return (m->comandi & PH_SRV_MEDIA_PUO_INDIETRO) != 0;
    default: return false;
    }
}

const char *ph_dsk_media_pacchetto(const PhSrvMedia *m)
{
    if (!ph_dsk_media_attivo(m) || memchr(m->pacchetto, '\0', sizeof m->pacchetto) == NULL)
        return NULL;
    return ph_dsk_pacchetto_valido(m->pacchetto) ? m->pacchetto : NULL;
}
