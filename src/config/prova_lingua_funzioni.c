/* Prove delle funzioni di lingua con una tabella finta: ph_t, scelta della
 * lingua dall'ambiente, composizione dei segnaposto (anche con modelli
 * ostili). Le chiamate a ph_t passano da un puntatore a funzione cosi' la
 * scansione dei sorgenti (prova_lingua) non le scambia per testi del progetto. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config/lingua.h"

static int errori;

#define CONTROLLA(cond)                                                          \
    do {                                                                         \
        if (!(cond)) {                                                           \
            fprintf(stderr, "%s:%d: fallito: %s\n", __FILE__, __LINE__, #cond); \
            errori++;                                                            \
        }                                                                        \
    } while (0)

#define SENT ((const char *)NULL)

/* PH_T_ non traduce. Passa da un alias per non far vedere alla scansione dei
 * sorgenti un testo che non e' nella tabella vera. */
#define MARCA PH_T_

static void prova_t(void)
{
    CONTROLLA(strcmp(MARCA("Ciao"), "Ciao") == 0);
    ph_lingua_imposta(PH_LINGUA_EN);
    CONTROLLA(strcmp(MARCA("Ciao"), "Ciao") == 0);
    ph_lingua_imposta(PH_LINGUA_IT);
    const char *(*t)(const char *) = ph_t;
    ph_lingua_imposta(PH_LINGUA_IT);
    CONTROLLA(strcmp(t("Ciao"), "Ciao") == 0);
    CONTROLLA(strcmp(t("Mai tradotto"), "Mai tradotto") == 0);
    ph_lingua_imposta(PH_LINGUA_EN);
    CONTROLLA(ph_lingua_attiva() == PH_LINGUA_EN);
    CONTROLLA(strcmp(t("Ciao"), "Hello") == 0);
    CONTROLLA(strcmp(t("Copiati {numero} file"), "{numero} files copied") == 0);
    CONTROLLA(strcmp(t("Mai tradotto"), "Mai tradotto") == 0); /* ricade sull'italiano */
    CONTROLLA(strcmp(t(""), "") == 0);
    CONTROLLA(strcmp(t(NULL), "") == 0);
    CONTROLLA(ph_lingua_traduzione_en("Ciao") != NULL);
    CONTROLLA(ph_lingua_traduzione_en("Ciaoo") == NULL);
    CONTROLLA(ph_lingua_traduzione_en("Cia") == NULL);
    CONTROLLA(ph_lingua_traduzione_en(NULL) == NULL);
    ph_lingua_imposta(PH_LINGUA_IT);
}

static void prova_ambiente(void)
{
    struct {
        const char *all, *msg, *lang;
        PhLingua atteso;
    } casi[] = {
        {NULL, NULL, "it_IT.UTF-8", PH_LINGUA_IT},
        {NULL, NULL, "it", PH_LINGUA_IT},
        {NULL, NULL, "it@euro", PH_LINGUA_IT},
        {NULL, NULL, "en_US.UTF-8", PH_LINGUA_EN},
        {NULL, NULL, "C", PH_LINGUA_EN},
        {NULL, NULL, "italian", PH_LINGUA_EN}, /* non e' un codice di lingua */
        {NULL, NULL, "ita_IT", PH_LINGUA_EN},
        {NULL, NULL, NULL, PH_LINGUA_EN},
        {NULL, NULL, "", PH_LINGUA_EN},
        {NULL, "it_IT", "en_US", PH_LINGUA_IT},     /* LC_MESSAGES batte LANG */
        {NULL, "en_US", "it_IT", PH_LINGUA_EN},
        {"en_US", "it_IT", "it_IT", PH_LINGUA_EN},  /* LC_ALL batte tutto */
        {"", "it_IT", "en_US", PH_LINGUA_IT},       /* vuoto = come non impostato */
    };
    for (size_t i = 0; i < sizeof casi / sizeof casi[0]; i++) {
        const char *nomi[] = {"LC_ALL", "LC_MESSAGES", "LANG"};
        const char *val[] = {casi[i].all, casi[i].msg, casi[i].lang};
        for (int k = 0; k < 3; k++) {
            if (val[k] != NULL)
                setenv(nomi[k], val[k], 1);
            else
                unsetenv(nomi[k]);
        }
        CONTROLLA(ph_lingua_da_ambiente() == casi[i].atteso);
    }
    /* L'app e' solo in inglese: ne' la preferenza (anche un vecchio "it") ne'
     * l'ambiente del PC cambiano la lingua attiva. */
    PhCfgLingua prefs[] = {PH_CFG_LINGUA_AUTO, PH_CFG_LINGUA_IT, PH_CFG_LINGUA_EN};
    const char *ambienti[] = {"it_IT.UTF-8", "de_DE", "en_US", "C"};
    for (size_t a = 0; a < sizeof ambienti / sizeof ambienti[0]; a++) {
        setenv("LANG", ambienti[a], 1);
        unsetenv("LC_ALL");
        unsetenv("LC_MESSAGES");
        for (size_t k = 0; k < sizeof prefs / sizeof prefs[0]; k++) {
            ph_lingua_imposta(PH_LINGUA_IT); /* partenza diversa: deve tornare inglese */
            CONTROLLA(ph_lingua_inizializza(prefs[k]) == PH_LINGUA_EN);
            CONTROLLA(ph_lingua_attiva() == PH_LINGUA_EN);
        }
    }
    ph_lingua_imposta(PH_LINGUA_IT);
}

static void prova_componi(void)
{
    char b[128];
    size_t n;

    n = ph_lingua_componi(b, sizeof b, "Copiati {numero} file di {nome}", "numero", "3", "nome", "A", SENT);
    CONTROLLA(strcmp(b, "Copiati 3 file di A") == 0 && n == strlen(b));
    /* ordine diverso e ripetizione */
    ph_lingua_componi(b, sizeof b, "{nome}: {numero} ({numero}) {nome}", "numero", "3", "nome", "A", SENT);
    CONTROLLA(strcmp(b, "A: 3 (3) A") == 0);
    /* nessuna coppia, segnaposto sconosciuto o valore NULL */
    ph_lingua_componi(b, sizeof b, "x {sconosciuto} y", SENT);
    CONTROLLA(strcmp(b, "x {sconosciuto} y") == 0);
    /* valore NULL: errore del chiamante, uscita vuota e (size_t)-1 */
    strcpy(b, "sporco");
    n = ph_lingua_componi(b, sizeof b, "x{a}y", "a", (const char *)NULL, SENT);
    CONTROLLA(n == (size_t)-1 && b[0] == '\0');
    n = ph_lingua_componi(NULL, 0, "x{a}y", "a", (const char *)NULL, SENT);
    CONTROLLA(n == (size_t)-1);
    /* graffe letterali e malformate */
    ph_lingua_componi(b, sizeof b, "{{a}} {{{a}}} { } {1} {a", "a", "V", SENT);
    CONTROLLA(strcmp(b, "{a} {V} { } {1} {a") == 0);
    /* il modello non e' una stringa di formato: % resta com'e' */
    ph_lingua_componi(b, sizeof b, "100%s %n %x {a}%d", "a", "V", SENT);
    CONTROLLA(strcmp(b, "100%s %n %x V%d") == 0);
    /* un valore con graffe o % non viene rielaborato */
    ph_lingua_componi(b, sizeof b, "{a}", "a", "{a}%s{{", SENT);
    CONTROLLA(strcmp(b, "{a}%s{{") == 0);
    /* modello NULL e vuoto */
    n = ph_lingua_componi(b, sizeof b, NULL, SENT);
    CONTROLLA(n == 0 && b[0] == '\0');
    /* troncamento: lunghezza richiesta, sempre terminato */
    n = ph_lingua_componi(b, 5, "abcdefghij", SENT);
    CONTROLLA(n == 10 && strcmp(b, "abcd") == 0);
    /* capacita zero: niente scritto, ma la lunghezza si */
    n = ph_lingua_componi(NULL, 0, "ab{a}", "a", "cde", SENT);
    CONTROLLA(n == 5);
    n = ph_lingua_componi(b, 1, "abc", SENT);
    CONTROLLA(n == 3 && b[0] == '\0');
    /* troncamento su confine UTF-8: "e" accentata = 2 byte */
    n = ph_lingua_componi(b, 4, "ab\xC3\xA8" "cd", SENT); /* "abecd" con e accentata; spazio per 3 byte */
    CONTROLLA(n == 6 && strcmp(b, "ab") == 0);
    n = ph_lingua_componi(b, 5, "ab\xC3\xA8" "cd", SENT);
    CONTROLLA(n == 6 && strcmp(b, "ab\xC3\xA8") == 0);
    n = ph_lingua_componi(b, 3, "\xE2\x82\xAC" "x", SENT); /* euro, 3 byte, non entra */
    CONTROLLA(n == 4 && b[0] == '\0');
    /* piu' di 16 coppie: le altre sono ignorate, nessun crash */
    ph_lingua_componi(b, sizeof b, "{a}", "n0", "0", "n1", "1", "n2", "2", "n3", "3", "n4", "4", "n5", "5",
                      "n6", "6", "n7", "7", "n8", "8", "n9", "9", "n10", "10", "n11", "11", "n12", "12",
                      "n13", "13", "n14", "14", "n15", "15", "n16", "16", "a", "V", SENT);
    CONTROLLA(strcmp(b, "{a}") == 0);
    /* nome lunghissimo: non e' un segnaposto, resta com'e' */
    char lungo[200];
    memset(lungo, 'a', sizeof lungo);
    lungo[0] = '{';
    lungo[sizeof lungo - 2] = '}';
    lungo[sizeof lungo - 1] = '\0';
    char grande[300];
    n = ph_lingua_componi(grande, sizeof grande, lungo, SENT);
    CONTROLLA(n == strlen(lungo) && strcmp(grande, lungo) == 0);
}

static void prova_segnaposto(void)
{
    CONTROLLA(ph_lingua_segnaposto_uguali("Ciao", "Hello") == 1);
    CONTROLLA(ph_lingua_segnaposto_uguali("{a} e {b}", "{b} and {a}") == 1);
    CONTROLLA(ph_lingua_segnaposto_uguali("{a} {a}", "{a}") == 0);
    CONTROLLA(ph_lingua_segnaposto_uguali("{a}", "{b}") == 0);
    CONTROLLA(ph_lingua_segnaposto_uguali("{a}", "x") == 0);
    CONTROLLA(ph_lingua_segnaposto_uguali("{{a}}", "x") == 1); /* graffe letterali */
    CONTROLLA(ph_lingua_segnaposto_uguali("{a", "{a") == -1);
    CONTROLLA(ph_lingua_segnaposto_uguali("a}", "a") == -1);
    CONTROLLA(ph_lingua_segnaposto_uguali("{}", "{}") == -1);
    CONTROLLA(ph_lingua_segnaposto_uguali("{1x}", "{1x}") == -1);
    CONTROLLA(ph_lingua_segnaposto_uguali("{a b}", "x") == -1);
    char molti[400] = "";
    for (int i = 0; i < 40; i++)
        strcat(molti, "{a}");
    CONTROLLA(ph_lingua_segnaposto_uguali(molti, molti) == -1); /* oltre 32 */
}

int main(void)
{
    prova_t();
    prova_ambiente();
    prova_componi();
    prova_segnaposto();
    if (errori == 0)
        printf("prova_lingua_funzioni: tutto bene\n");
    return errori == 0 ? 0 : 1;
}
