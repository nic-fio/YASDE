/* Logica pura della barra (F-03). Vedi logica.h. */
#include "shell/barra/logica.h"

#include <stdio.h>
#include <string.h>

#include "config/lingua.h"

PhBarraIndicatore ph_barra_indicatore(const PhShellFinestra *f)
{
    if (f->minimizzata)
        return PH_BARRA_IND_MINIMIZZATA;
    return f->attiva ? PH_BARRA_IND_ATTIVA : PH_BARRA_IND_APERTA;
}

/* Un taglio per mancanza di spazio puo' cadere in mezzo a un carattere
 * multibyte: si torna all'ultimo carattere intero. */
static void taglia_utf8(char *s)
{
    const char *fine = NULL;
    if (!g_utf8_validate(s, -1, &fine))
        s[fine - s] = '\0';
}

/* Il modello e' sempre un ph_t letterale; il nome arriva dal telefono e non e'
 * mai usato come formato. Se la composizione fallisce resta il solo nome, che
 * e' comunque un'etichetta utile. */
static void componi_nome(char *out, size_t capacita, const char *modello, const char *nome)
{
    if (capacita == 0)
        return;
    if (ph_lingua_componi(out, capacita, modello, "nome", nome, (const char *)NULL) ==
        (size_t)-1) {
        snprintf(out, capacita, "%s", nome);
    }
    taglia_utf8(out);
}

void ph_barra_etichetta(const PhShellFinestra *f, char *out, size_t capacita)
{
    if (f->minimizzata)
        componi_nome(out, capacita, ph_t("{nome} (in pausa)"), f->titolo);
    else
        componi_nome(out, capacita, ph_t("{nome} (aperta)"), f->titolo);
}

void ph_barra_suggerimento(const PhShellFinestra *f, char *out, size_t capacita)
{
    if (capacita == 0)
        return;
    if (f->minimizzata)
        componi_nome(out, capacita, ph_t("{nome} · in pausa, stream sospeso"), f->titolo);
    else {
        snprintf(out, capacita, "%s", f->titolo);
        taglia_utf8(out);
    }
}

unsigned ph_barra_ms_al_minuto(int secondi, int microsecondi)
{
    if (secondi < 0)
        secondi = 0;
    if (secondi > 59)
        secondi = 59;
    if (microsecondi < 0)
        microsecondi = 0;
    if (microsecondi > 999999)
        microsecondi = 999999;
    int passati_ms = secondi * 1000 + microsecondi / 1000;
    return (unsigned)(60000 - passati_ms);
}

void ph_barra_ora_testo(int ore, int minuti, char *out, size_t capacita)
{
    if (ore < 0)
        ore = 0;
    if (ore > 23)
        ore = 23;
    if (minuti < 0)
        minuti = 0;
    if (minuti > 59)
        minuti = 59;
    snprintf(out, capacita, "%02d:%02d", ore, minuti);
}

void ph_barra_etichetta_orologio(const char *ora, const char *data, char *out, size_t capacita)
{
    if (capacita == 0)
        return;
    if (data != NULL && *data != '\0')
        snprintf(out, capacita, "%s, %s", ora, data);
    else
        snprintf(out, capacita, "%s", ora);
    taglia_utf8(out);
}

size_t ph_barra_ordina(const char (*fissate)[PH_CFG_LUN_APP], size_t n_fissate,
                       const PhShellFinestra *finestre, size_t n_finestre, PhBarraElemento *out,
                       size_t max)
{
    size_t k = 0;
    /* Quali finestre sono gia' state abbinate a una fissata (fino a 256). */
    bool presa[256] = {false};
    if (n_finestre > 256)
        n_finestre = 256;
    for (size_t i = 0; i < n_fissate && k < max; i++) {
        if (fissate[i][0] == '\0')
            continue;
        int trovata = -1;
        for (size_t j = 0; j < n_finestre; j++) {
            if (!presa[j] && !finestre[j].specchio &&
                strncmp(finestre[j].pacchetto, fissate[i], PH_CFG_LUN_APP) == 0) {
                trovata = (int)j;
                presa[j] = true;
                break;
            }
        }
        out[k].finestra = trovata;
        out[k].fissata = (int)i;
        k++;
    }
    for (size_t j = 0; j < n_finestre && k < max; j++) {
        if (presa[j])
            continue;
        out[k].finestra = (int)j;
        out[k].fissata = -1;
        k++;
    }
    return k;
}

void ph_barra_etichetta_chiusa(const char *nome, char *out, size_t capacita)
{
    if (capacita == 0)
        return;
    snprintf(out, capacita, "%s", nome);
    taglia_utf8(out);
}

void ph_barra_suggerimento_chiusa(const char *nome, char *out, size_t capacita)
{
    componi_nome(out, capacita, ph_t("Apri {nome}"), nome);
}

size_t ph_barra_menu(bool aperta, bool fissata, PhBarraMenuVoce out[2])
{
    size_t n = 0;
    if (aperta)
        out[n++] = PH_BARRA_MENU_CHIUDI;
    out[n++] = fissata ? PH_BARRA_MENU_TOGLI : PH_BARRA_MENU_FISSA;
    return n;
}

const char *ph_barra_menu_testo(PhBarraMenuVoce v)
{
    switch (v) {
    case PH_BARRA_MENU_CHIUDI: return ph_t("Chiudi app");
    case PH_BARRA_MENU_FISSA: return ph_t("Fissa sulla barra");
    default: return ph_t("Togli dalla barra");
    }
}

/* Gli stessi testi del pezzo desktop (data/en/shell-desktop.txt). */
static const char *const GIORNI[] = {PH_T_("Lunedì"),   PH_T_("Martedì"), PH_T_("Mercoledì"),
                                     PH_T_("Giovedì"),  PH_T_("Venerdì"), PH_T_("Sabato"),
                                     PH_T_("Domenica")};
static const char *const MESI[] = {PH_T_("gennaio"),  PH_T_("febbraio"), PH_T_("marzo"),
                                   PH_T_("aprile"),   PH_T_("maggio"),   PH_T_("giugno"),
                                   PH_T_("luglio"),   PH_T_("agosto"),   PH_T_("settembre"),
                                   PH_T_("ottobre"),  PH_T_("novembre"), PH_T_("dicembre")};
/* Intestazione delle colonne del calendario (settimana da lunedi'). */
static const char *const GIORNI_BREVI[] = {PH_T_("lun"), PH_T_("mar"), PH_T_("mer"), PH_T_("gio"),
                                           PH_T_("ven"), PH_T_("sab"), PH_T_("dom")};

const char *ph_barra_nome_mese(int mese)
{
    return mese >= 1 && mese <= 12 ? ph_tv(MESI[mese - 1]) : "";
}

const char *ph_barra_giorno_breve(int colonna)
{
    return colonna >= 0 && colonna <= 6 ? ph_tv(GIORNI_BREVI[colonna]) : "";
}

void ph_barra_mese(int anno, int mese, int *colonna_primo, int *giorni)
{
    *colonna_primo = 0;
    *giorni = 0;
    if (anno < 1 || anno > 9999 || mese < 1 || mese > 12)
        return;
    GDate *d = g_date_new_dmy(1, (GDateMonth)mese, (GDateYear)anno);
    if (d == NULL)
        return;
    *colonna_primo = (int)g_date_get_weekday(d) - 1; /* 1 = lunedi' */
    *giorni = (int)g_date_get_days_in_month((GDateMonth)mese, (GDateYear)anno);
    g_date_free(d);
}

void ph_barra_mese_sposta(int *anno, int *mese, int delta_mesi)
{
    int totale = *anno * 12 + (*mese - 1) + delta_mesi;
    if (totale < 1 * 12)
        totale = 12;
    if (totale > 9999 * 12 + 11)
        totale = 9999 * 12 + 11;
    *anno = totale / 12;
    *mese = totale % 12 + 1;
}

void ph_barra_data_lunga(int giorno_settimana, int giorno, int mese, int anno, char *out,
                         size_t capacita)
{
    if (capacita == 0)
        return;
    out[0] = '\0';
    if (giorno_settimana < 1 || giorno_settimana > 7 || mese < 1 || mese > 12 || giorno < 1 ||
        giorno > 31 || anno < 1 || anno > 9999)
        return;
    char num[16], an[16], parte[96];
    snprintf(num, sizeof num, "%d", giorno);
    snprintf(an, sizeof an, "%d", anno);
    if (ph_lingua_componi(parte, sizeof parte, ph_t("{giorno} {numero} {mese}"), "giorno",
                          ph_tv(GIORNI[giorno_settimana - 1]), "numero", num, "mese",
                          ph_tv(MESI[mese - 1]), (const char *)NULL) == (size_t)-1)
        return;
    if (ph_lingua_componi(out, capacita, ph_t("{data} {anno}"), "data", parte, "anno", an,
                          (const char *)NULL) == (size_t)-1)
        out[0] = '\0';
    taglia_utf8(out);
}
