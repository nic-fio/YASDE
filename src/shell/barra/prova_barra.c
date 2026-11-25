/* Prove di logica di shell-barra (F-03), SENZA schermo: funzioni pure di
 * logica.c (indicatore, testi, minuto) e lettura del CSS. */
#include <stdio.h>
#include <string.h>

#include "config/lingua.h"
#include "shell/barra/logica.h"
#include "shell/prova_css.h"
#include "shell/shell.h"

void ph_risorse_barra_register_resource(void);

static int errori;

#define CONTROLLA(cond)                                                                      \
    do {                                                                                     \
        if (!(cond)) {                                                                       \
            fprintf(stderr, "prova_barra: riga %d: %s\n", __LINE__, #cond);                  \
            errori++;                                                                        \
        }                                                                                    \
    } while (0)

static PhShellFinestra finestra(const char *titolo, bool attiva, bool minimizzata, bool specchio)
{
    PhShellFinestra f;
    memset(&f, 0, sizeof f);
    f.id = 1;
    snprintf(f.titolo, sizeof f.titolo, "%s", titolo);
    f.attiva = attiva;
    f.minimizzata = minimizzata;
    f.specchio = specchio;
    return f;
}

static void prova_indicatore_e_testi(void)
{
    PhShellFinestra aperta = finestra("Messaggi", false, false, false);
    PhShellFinestra attiva = finestra("Messaggi", true, false, false);
    PhShellFinestra minimizzata = finestra("Messaggi", false, true, false);
    /* Un gestore che segna attiva una finestra gia' minimizzata: vale la pausa. */
    PhShellFinestra incoerente = finestra("Messaggi", true, true, false);
    PhShellFinestra specchio = finestra("Schermo del telefono", true, false, true);
    char e[256], s[320];

    CONTROLLA(ph_barra_indicatore(&aperta) == PH_BARRA_IND_APERTA);
    CONTROLLA(ph_barra_indicatore(&attiva) == PH_BARRA_IND_ATTIVA);
    CONTROLLA(ph_barra_indicatore(&minimizzata) == PH_BARRA_IND_MINIMIZZATA);
    CONTROLLA(ph_barra_indicatore(&incoerente) == PH_BARRA_IND_MINIMIZZATA);
    CONTROLLA(ph_barra_indicatore(&specchio) == PH_BARRA_IND_ATTIVA);

    ph_barra_etichetta(&aperta, e, sizeof e);
    ph_barra_suggerimento(&aperta, s, sizeof s);
    CONTROLLA(strcmp(e, "Messaggi (aperta)") == 0);
    CONTROLLA(strcmp(s, "Messaggi") == 0);

    ph_barra_etichetta(&attiva, e, sizeof e);
    CONTROLLA(strcmp(e, "Messaggi (aperta)") == 0);

    ph_barra_etichetta(&minimizzata, e, sizeof e);
    ph_barra_suggerimento(&minimizzata, s, sizeof s);
    CONTROLLA(strcmp(e, "Messaggi (in pausa)") == 0);
    CONTROLLA(strcmp(s, "Messaggi \xC2\xB7 in pausa, stream sospeso") == 0);

    ph_barra_etichetta(&specchio, e, sizeof e);
    CONTROLLA(strcmp(e, "Schermo del telefono (aperta)") == 0);

    /* Buffer troppo piccolo: troncato e terminato, mai fuori dal buffer. */
    char piccolo[8];
    memset(piccolo, 'x', sizeof piccolo);
    ph_barra_etichetta(&aperta, piccolo, sizeof piccolo);
    CONTROLLA(memchr(piccolo, '\0', sizeof piccolo) != NULL);
    ph_barra_suggerimento(&minimizzata, piccolo, sizeof piccolo);
    CONTROLLA(memchr(piccolo, '\0', sizeof piccolo) != NULL);

    /* Nome multibyte che non ci sta: troncato ma UTF-8 valido. Provato con ogni
     * misura, perche' il taglio cada anche in mezzo a un carattere. */
    PhShellFinestra uni = finestra("\xC3\xA8\xC3\xA8\xC3\xA8\xC3\xA8\xC3\xA8\xC3\xA8", false, true, false);
    for (size_t cap = 1; cap < 40; cap++) {
        char buf[40];
        ph_barra_etichetta(&uni, buf, cap);
        CONTROLLA(strlen(buf) < cap);
        CONTROLLA(g_utf8_validate(buf, -1, NULL));
        ph_barra_suggerimento(&uni, buf, cap);
        CONTROLLA(strlen(buf) < cap);
        CONTROLLA(g_utf8_validate(buf, -1, NULL));
    }

    /* Un nome con segnaposto o formato non viene interpretato. */
    PhShellFinestra strano = finestra("%s {nome} %n", false, false, false);
    ph_barra_etichetta(&strano, e, sizeof e);
    CONTROLLA(strncmp(e, "%s {nome} %n", 12) == 0);

    /* In inglese le stesse frasi (traduzioni di shell-comune.txt). */
    ph_lingua_imposta(PH_LINGUA_EN);
    ph_barra_etichetta(&minimizzata, e, sizeof e);
    CONTROLLA(strcmp(e, "Messaggi (paused)") == 0);
    ph_barra_etichetta(&aperta, e, sizeof e);
    CONTROLLA(strcmp(e, "Messaggi (open)") == 0);
    ph_lingua_imposta(PH_LINGUA_IT);
}

static void prova_orologio(void)
{
    char e[64];
    ph_barra_etichetta_orologio("07:05", "luned\xC3\xAC 23 novembre 2026", e, sizeof e);
    CONTROLLA(strcmp(e, "07:05, luned\xC3\xAC 23 novembre 2026") == 0);
    ph_barra_etichetta_orologio("07:05", "", e, sizeof e);
    CONTROLLA(strcmp(e, "07:05") == 0);
    ph_barra_etichetta_orologio("07:05", NULL, e, sizeof e);
    CONTROLLA(strcmp(e, "07:05") == 0);
    /* Taglio in mezzo a un carattere multibyte: resta UTF-8 valido. */
    for (size_t cap = 1; cap < 20; cap++) {
        char b[20];
        ph_barra_etichetta_orologio("07:05", "luned\xC3\xAC \xC3\xA8\xC3\xA8", b, cap);
        CONTROLLA(strlen(b) < cap);
        CONTROLLA(g_utf8_validate(b, -1, NULL));
    }
}

static PhShellFinestra fin_id(unsigned id, const char *pac, bool specchio)
{
    PhShellFinestra f;
    memset(&f, 0, sizeof f);
    f.id = id;
    snprintf(f.pacchetto, sizeof f.pacchetto, "%s", pac);
    snprintf(f.titolo, sizeof f.titolo, "%s", pac);
    f.specchio = specchio;
    return f;
}

static void prova_ordine(void)
{
    char fis[3][PH_CFG_LUN_APP] = {"b", "c", "a"};
    PhShellFinestra f[] = {fin_id(1, "a", false), fin_id(2, "d", false), fin_id(3, "", true),
                           fin_id(4, "c", false)};
    PhBarraElemento e[16];
    size_t n = ph_barra_ordina((const char (*)[PH_CFG_LUN_APP])fis, 3, f, 4, e, 16);
    /* Fissate nell'ordine dell'elenco: b (chiusa), c (finestra 3), a (finestra 0);
     * poi le altre aperte nell'ordine di apertura: d, specchio. */
    CONTROLLA(n == 5);
    CONTROLLA(e[0].fissata == 0 && e[0].finestra == -1);
    CONTROLLA(e[1].fissata == 1 && e[1].finestra == 3);
    CONTROLLA(e[2].fissata == 2 && e[2].finestra == 0);
    CONTROLLA(e[3].fissata == -1 && e[3].finestra == 1);
    CONTROLLA(e[4].fissata == -1 && e[4].finestra == 2);

    /* Nessuna fissata: solo le finestre, in ordine. */
    n = ph_barra_ordina((const char (*)[PH_CFG_LUN_APP])fis, 0, f, 4, e, 16);
    CONTROLLA(n == 4 && e[0].finestra == 0 && e[3].finestra == 3 && e[0].fissata == -1);
    /* Niente finestre: tutte le fissate chiuse. */
    n = ph_barra_ordina((const char (*)[PH_CFG_LUN_APP])fis, 3, f, 0, e, 16);
    CONTROLLA(n == 3 && e[0].finestra == -1 && e[2].fissata == 2);
    /* Una fissata con due finestre: la seconda resta fra le non fissate. */
    PhShellFinestra due[] = {fin_id(1, "a", false), fin_id(2, "a", false)};
    n = ph_barra_ordina((const char (*)[PH_CFG_LUN_APP])fis, 3, due, 2, e, 16);
    CONTROLLA(n == 4 && e[2].finestra == 0 && e[3].finestra == 1 && e[3].fissata == -1);
    /* Pacchetto vuoto fra le fissate: ignorato. Tetto di max rispettato. */
    char vuoto[2][PH_CFG_LUN_APP] = {"", "a"};
    n = ph_barra_ordina((const char (*)[PH_CFG_LUN_APP])vuoto, 2, f, 4, e, 16);
    CONTROLLA(n == 4 && e[0].fissata == 1);
    n = ph_barra_ordina((const char (*)[PH_CFG_LUN_APP])fis, 3, f, 4, e, 2);
    CONTROLLA(n == 2);
}

static void prova_menu_e_data(void)
{
    PhBarraMenuVoce m[2];
    CONTROLLA(ph_barra_menu(true, false, m) == 2 && m[0] == PH_BARRA_MENU_CHIUDI &&
              m[1] == PH_BARRA_MENU_FISSA);
    CONTROLLA(ph_barra_menu(true, true, m) == 2 && m[0] == PH_BARRA_MENU_CHIUDI &&
              m[1] == PH_BARRA_MENU_TOGLI);
    CONTROLLA(ph_barra_menu(false, false, m) == 1 && m[0] == PH_BARRA_MENU_FISSA);
    CONTROLLA(ph_barra_menu(false, true, m) == 1 && m[0] == PH_BARRA_MENU_TOGLI);
    CONTROLLA(strcmp(ph_barra_menu_testo(PH_BARRA_MENU_CHIUDI), "Chiudi app") == 0);
    CONTROLLA(strcmp(ph_barra_menu_testo(PH_BARRA_MENU_FISSA), "Fissa sulla barra") == 0);
    CONTROLLA(strcmp(ph_barra_menu_testo(PH_BARRA_MENU_TOGLI), "Togli dalla barra") == 0);

    char s[96];
    ph_barra_etichetta_chiusa("Note", s, sizeof s);
    CONTROLLA(strcmp(s, "Note") == 0);
    ph_barra_suggerimento_chiusa("Note", s, sizeof s);
    CONTROLLA(strcmp(s, "Apri Note") == 0);

    /* 23 novembre 2026 e' un lunedi'. */
    ph_barra_data_lunga(1, 23, 11, 2026, s, sizeof s);
    CONTROLLA(strcmp(s, "Luned\xC3\xAC 23 novembre 2026") == 0);
    ph_lingua_imposta(PH_LINGUA_EN);
    ph_barra_data_lunga(1, 23, 11, 2026, s, sizeof s);
    CONTROLLA(strcmp(s, "Monday, November 23, 2026") == 0);
    ph_lingua_imposta(PH_LINGUA_IT);
    ph_barra_data_lunga(0, 23, 11, 2026, s, sizeof s);
    CONTROLLA(s[0] == '\0');
    ph_barra_data_lunga(1, 32, 11, 2026, s, sizeof s);
    CONTROLLA(s[0] == '\0');
    ph_barra_data_lunga(1, 23, 13, 2026, s, sizeof s);
    CONTROLLA(s[0] == '\0');
    for (size_t cap = 1; cap < 40; cap++) {
        char b[40];
        ph_barra_data_lunga(1, 23, 11, 2026, b, cap);
        CONTROLLA(strlen(b) < cap);
        CONTROLLA(g_utf8_validate(b, -1, NULL));
    }
}

static void prova_calendario(void)
{
    int col, g;
    ph_barra_mese(2026, 11, &col, &g); /* il 1 novembre 2026 e' una domenica */
    CONTROLLA(col == 6 && g == 30);
    ph_barra_mese(2024, 2, &col, &g); /* bisestile, 1 febbraio 2024 = giovedi' */
    CONTROLLA(col == 3 && g == 29);
    ph_barra_mese(2023, 2, &col, &g);
    CONTROLLA(g == 28);
    ph_barra_mese(2026, 6, &col, &g); /* 1 giugno 2026 = lunedi' */
    CONTROLLA(col == 0 && g == 30);
    ph_barra_mese(2026, 13, &col, &g);
    CONTROLLA(col == 0 && g == 0);
    ph_barra_mese(0, 1, &col, &g);
    CONTROLLA(g == 0);

    int a = 2026, m = 11;
    ph_barra_mese_sposta(&a, &m, 2);
    CONTROLLA(a == 2027 && m == 1);
    ph_barra_mese_sposta(&a, &m, -1);
    CONTROLLA(a == 2026 && m == 12);
    ph_barra_mese_sposta(&a, &m, -12);
    CONTROLLA(a == 2025 && m == 12);
    a = 1;
    m = 1;
    ph_barra_mese_sposta(&a, &m, -5);
    CONTROLLA(a == 1 && m == 1);
    a = 9999;
    m = 12;
    ph_barra_mese_sposta(&a, &m, 3);
    CONTROLLA(a == 9999 && m == 12);

    CONTROLLA(strcmp(ph_barra_nome_mese(11), "novembre") == 0);
    CONTROLLA(ph_barra_nome_mese(0)[0] == '\0');
    CONTROLLA(strcmp(ph_barra_giorno_breve(0), "lun") == 0);
    CONTROLLA(strcmp(ph_barra_giorno_breve(6), "dom") == 0);
    CONTROLLA(ph_barra_giorno_breve(7)[0] == '\0');
    ph_lingua_imposta(PH_LINGUA_EN);
    CONTROLLA(strcmp(ph_barra_nome_mese(11), "November") == 0);
    CONTROLLA(strcmp(ph_barra_giorno_breve(0), "Mon") == 0);
    ph_lingua_imposta(PH_LINGUA_IT);
}

static void prova_minuto(void)
{
    CONTROLLA(ph_barra_ms_al_minuto(0, 0) == 60000);
    CONTROLLA(ph_barra_ms_al_minuto(0, 1000) == 59999);
    CONTROLLA(ph_barra_ms_al_minuto(30, 0) == 30000);
    CONTROLLA(ph_barra_ms_al_minuto(30, 500000) == 29500);
    CONTROLLA(ph_barra_ms_al_minuto(59, 999999) == 1);
    CONTROLLA(ph_barra_ms_al_minuto(59, 0) == 1000);
    /* Fuori campo (secondo intercalare, valori sbagliati): sempre 1..60000. */
    CONTROLLA(ph_barra_ms_al_minuto(60, 0) == 1000);
    CONTROLLA(ph_barra_ms_al_minuto(-5, -5) == 60000);
    CONTROLLA(ph_barra_ms_al_minuto(100, 2000000) == 1);

    char t[8];
    ph_barra_ora_testo(7, 5, t, sizeof t);
    CONTROLLA(strcmp(t, "07:05") == 0);
    ph_barra_ora_testo(23, 59, t, sizeof t);
    CONTROLLA(strcmp(t, "23:59") == 0);
    ph_barra_ora_testo(0, 0, t, sizeof t);
    CONTROLLA(strcmp(t, "00:00") == 0);
    ph_barra_ora_testo(99, -1, t, sizeof t);
    CONTROLLA(strcmp(t, "23:00") == 0);
}

/* Regola della fase 5: nel CSS del pezzo nessun colore di contenuto scritto a mano, salvo
 * l'eccezione marcata colore-dato. Il rilevatore e' quello comune (shell/prova_css.h). */
static void prova_css_senza_colori(void)
{
    CONTROLLA(ph_prova_css_risorsa("/it/yasde/shell/barra.css", "barra.css", true) == 0);
    CONTROLLA(ph_prova_css_autoprova() == 0);
}

int main(void)
{
    ph_lingua_imposta(PH_LINGUA_IT);
    prova_indicatore_e_testi();
    prova_orologio();
    prova_ordine();
    prova_menu_e_data();
    prova_calendario();
    prova_minuto();

    ph_risorse_barra_register_resource();
    if (ph_shell_stile_controlla("/it/yasde/shell/barra.css") != 0) {
        fprintf(stderr, "barra.css: errori di sintassi\n");
        errori++;
    }
    prova_css_senza_colori();
    if (errori == 0)
        printf("prova_barra: tutto bene\n");
    return errori == 0 ? 0 : 1;
}
