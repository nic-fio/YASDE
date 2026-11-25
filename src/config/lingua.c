/* Lingua attiva, scelta dall'ambiente e ricerca delle traduzioni. */
#include "config/lingua.h"

#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

#include "config/lingua_tabella.h"

static _Atomic int lingua_attiva = PH_LINGUA_IT;

PhLingua ph_lingua_da_ambiente(void)
{
    static const char *const variabili[] = {"LC_ALL", "LC_MESSAGES", "LANG"};
    for (size_t i = 0; i < sizeof variabili / sizeof variabili[0]; i++) {
        const char *v = getenv(variabili[i]);
        if (v == NULL || v[0] == '\0')
            continue;
        /* Il primo valore non vuoto decide, come fa il sistema. */
        bool it = v[0] == 'i' && v[1] == 't' && (v[2] == '\0' || v[2] == '_' || v[2] == '.' || v[2] == '@');
        return it ? PH_LINGUA_IT : PH_LINGUA_EN;
    }
    return PH_LINGUA_EN;
}

PhLingua ph_lingua_inizializza(PhCfgLingua preferenza)
{
    /* Decisione del 24 nov 2026: l'app e' solo in inglese. La preferenza
     * (anche un vecchio "it" o "auto" nel file) si legge e si ignora; l'ambiente
     * del PC non conta. */
    (void)preferenza;
    ph_lingua_imposta(PH_LINGUA_EN);
    return PH_LINGUA_EN;
}

void ph_lingua_imposta(PhLingua lingua)
{
    atomic_store(&lingua_attiva, lingua == PH_LINGUA_EN ? PH_LINGUA_EN : PH_LINGUA_IT);
}

PhLingua ph_lingua_attiva(void)
{
    return (PhLingua)atomic_load(&lingua_attiva);
}

const char *ph_lingua_traduzione_en(const char *italiano)
{
    if (italiano == NULL)
        return NULL;
    size_t lo = 0, hi = ph_lingua_tabella_en_n;
    while (lo < hi) {
        size_t m = lo + (hi - lo) / 2;
        int c = strcmp(italiano, ph_lingua_tabella_en[m].it);
        if (c == 0)
            return ph_lingua_tabella_en[m].en;
        if (c < 0)
            hi = m;
        else
            lo = m + 1;
    }
    return NULL;
}

const char *ph_tv(const char *italiano)
{
    return ph_t(italiano);
}

const char *ph_t(const char *italiano)
{
    if (italiano == NULL)
        return "";
    if (ph_lingua_attiva() == PH_LINGUA_IT)
        return italiano;
    const char *en = ph_lingua_traduzione_en(italiano);
    return en != NULL ? en : italiano;
}
