/* Prove di logica di shell-sistema (F-06, F-07, F-08), SENZA schermo: CSS,
 * testi ostili, notifiche nascoste (nascondi, aggiorna, dimentica), notifiche
 * nuove rispetto alla base, riga "{app} · {ora}", versione Android, riempimento
 * della batteria, riga di stato, attesa del pannello fisico (rifiuto del
 * telefono), testi del volume. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config/lingua.h"
#include "shell/prova_css.h"
#include "shell/shell.h"
#include "shell/sistema/logica.h"

void ph_risorse_sistema_register_resource(void);

static int errori;
#define CONTROLLA(c)                                                                               \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            fprintf(stderr, "prova_sistema: FALLITO riga %d: %s\n", __LINE__, #c);                 \
            errori++;                                                                              \
        }                                                                                          \
    } while (0)
#define UGUALI(a, b)                                                                               \
    do {                                                                                           \
        const char *a_ = (a), *b_ = (b);                                                           \
        if (strcmp(a_, b_) != 0) {                                                                 \
            fprintf(stderr, "prova_sistema: FALLITO riga %d: \"%s\" != \"%s\"\n", __LINE__, a_,    \
                    b_);                                                                           \
            errori++;                                                                              \
        }                                                                                          \
    } while (0)

/* Notifica di prova (testi finti, mai modificati). */
static PhSrvNotifica nota(const char *chiave, uint64_t ora_ms)
{
    PhSrvNotifica n = {0};
    n.chiave = (char *)chiave;
    n.pacchetto = (char *)"it.finto.messaggi";
    n.titolo = (char *)"Titolo";
    n.testo = (char *)"Testo";
    n.ora_ms = ora_ms;
    return n;
}

static void prova_testo_sicuro(void)
{
    char out[256];
    ph_sistema_testo_sicuro("Ciao\nmondo\t!", 100, out, sizeof out);
    UGUALI(out, "Ciao mondo !");
    ph_sistema_testo_sicuro("a\x01\x02\x1b[31mb", 100, out, sizeof out);
    UGUALI(out, "a [31mb");
    ph_sistema_testo_sicuro("\n\n  x  \r\n", 100, out, sizeof out);
    UGUALI(out, "x");
    /* Solo i controlli di direzione si tolgono; lo spazio nullo e il congiuntore (emoji composte) restano. */
    ph_sistema_testo_sicuro("a\xe2\x80\xae" "b\xe2\x80\x8b" "c", 100, out, sizeof out);
    UGUALI(out, "ab\xe2\x80\x8b" "c");
    ph_sistema_testo_sicuro("\xe2\x80\x8e" "x\xe2\x81\xa6" "y\xd8\x9c" "z\xe2\x80\x8f", 100, out, sizeof out);
    UGUALI(out, "xyz");
    ph_sistema_testo_sicuro("\xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\x92\xbb", 100, out, sizeof out); /* donna + ZWJ + computer */
    UGUALI(out, "\xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\x92\xbb");
    ph_sistema_testo_sicuro(NULL, 100, out, sizeof out);
    UGUALI(out, "");
    ph_sistema_testo_sicuro("abc", 0, out, sizeof out);
    UGUALI(out, "");
    out[0] = 'x';
    ph_sistema_testo_sicuro("abc", 10, out, 1);
    CONTROLLA(out[0] == '\0');

    /* UTF-8 rotto: l'esito e' valido e non cade fuori dal buffer. */
    ph_sistema_testo_sicuro("ab\xff\xfe" "cd\xc3", 100, out, sizeof out);
    CONTROLLA(g_utf8_validate(out, -1, NULL));
    CONTROLLA(strlen(out) > 0);

    /* Troppo lungo: finisce con "…" e il conto compre il puntino. */
    char lungo[400];
    memset(lungo, 'a', sizeof lungo - 1);
    lungo[sizeof lungo - 1] = '\0';
    ph_sistema_testo_sicuro(lungo, 10, out, sizeof out);
    CONTROLLA(g_utf8_strlen(out, -1) == 10);
    CONTROLLA(g_str_has_suffix(out, "\xE2\x80\xA6"));
    ph_sistema_testo_sicuro("abcdefghij", 10, out, sizeof out); /* esattamente al limite: intero */
    UGUALI(out, "abcdefghij");
    ph_sistema_testo_sicuro("abcdefghijk", 10, out, sizeof out);
    UGUALI(out, "abcdefghi\xE2\x80\xA6");
    ph_sistema_testo_sicuro("abcd efghij", 5, out, sizeof out); /* niente spazio prima di "…" */
    UGUALI(out, "abcd\xE2\x80\xA6");

    /* Buffer piccolo e caratteri a piu' byte: mai a meta' di un carattere. */
    char picc[8];
    ph_sistema_testo_sicuro("\xc3\xa0\xc3\xa0\xc3\xa0\xc3\xa0\xc3\xa0\xc3\xa0", 100, picc, sizeof picc);
    CONTROLLA(g_utf8_validate(picc, -1, NULL));
    CONTROLLA(strlen(picc) <= sizeof picc - 1);

    /* Un megabyte: non si legge tutto e non si rompe niente. */
    size_t mega = 1 << 20;
    char *enorme = malloc(mega + 1);
    CONTROLLA(enorme != NULL);
    if (enorme != NULL) {
        memset(enorme, '\n', mega);
        enorme[mega] = '\0';
        ph_sistema_testo_sicuro(enorme, 100, out, sizeof out);
        UGUALI(out, "");
        memset(enorme, 'x', mega);
        ph_sistema_testo_sicuro(enorme, 100, out, sizeof out);
        CONTROLLA(g_utf8_strlen(out, -1) == 100);
        free(enorme);
    }

    /* Segnaposto e formati restano testo: nessuna interpretazione. */
    ph_sistema_testo_sicuro("%s%n{percento}<b>x</b>", 100, out, sizeof out);
    UGUALI(out, "%s%n{percento}<b>x</b>");
}

static void prova_nascoste(void)
{
    PhNascoste *n = ph_nascoste_nuove();
    PhSrvNotifica v[2] = {nota("k1", 100), nota("k2", 200)};

    CONTROLLA(ph_nascoste_visibili(n, v, 2) == 2);
    CONTROLLA(!ph_nascoste_nascondi(n, NULL, 5));
    CONTROLLA(ph_nascoste_nascondi(n, "k1", 100));
    CONTROLLA(!ph_nascoste_nascondi(n, "k1", 100)); /* gia' nascosta: niente cambia */
    CONTROLLA(ph_nascoste_e_nascosta(n, "k1", 100));
    CONTROLLA(!ph_nascoste_e_nascosta(n, "k1", 101)); /* aggiornata: non piu' nascosta */
    CONTROLLA(!ph_nascoste_e_nascosta(n, "k2", 200));
    CONTROLLA(!ph_nascoste_e_nascosta(n, NULL, 100));
    CONTROLLA(ph_nascoste_visibili(n, v, 2) == 1);

    /* Il telefono aggiorna k1: torna visibile e l'insieme se ne dimentica. */
    PhSrvNotifica agg[2] = {nota("k1", 150), nota("k2", 200)};
    CONTROLLA(ph_nascoste_visibili(n, agg, 2) == 2);
    CONTROLLA(ph_nascoste_sincronizza(n, agg, 2, true));
    CONTROLLA(ph_nascoste_numero(n) == 0);
    CONTROLLA(!ph_nascoste_sincronizza(n, agg, 2, true));
    CONTROLLA(!ph_nascoste_e_nascosta(n, "k1", 100)); /* nemmeno se l'ora tornasse quella di prima */

    /* Una notifica sparita: la nascosta si dimentica. */
    CONTROLLA(ph_nascoste_nascondi(n, "k2", 200));
    CONTROLLA(ph_nascoste_sincronizza(n, agg, 1, true));
    CONTROLLA(ph_nascoste_numero(n) == 0);

    /* Telefono che non puo' leggere le notifiche: la fotografia vuota non
     * fa riapparire cio' che l'utente ha nascosto. */
    CONTROLLA(ph_nascoste_nascondi(n, "k1", 150));
    CONTROLLA(!ph_nascoste_sincronizza(n, NULL, 0, false));
    CONTROLLA(ph_nascoste_e_nascosta(n, "k1", 150));

    /* Nascondi tutte. */
    CONTROLLA(ph_nascoste_nascondi_tutte(n, agg, 2));
    CONTROLLA(ph_nascoste_visibili(n, agg, 2) == 0);
    CONTROLLA(!ph_nascoste_nascondi_tutte(n, agg, 2));
    PhSrvNotifica nuova[3] = {nota("k3", 300), agg[0], agg[1]};
    CONTROLLA(ph_nascoste_visibili(n, nuova, 3) == 1); /* una arrivata dopo e' visibile */

    PhSrvNotifica senza_chiave = nota(NULL, 1);
    CONTROLLA(!ph_nascoste_nascondi_tutte(n, &senza_chiave, 1));
    CONTROLLA(ph_nascoste_visibili(n, &senza_chiave, 1) == 1);
    ph_nascoste_libera(n);
}

#define REG(v, a, n, l, idx) ph_viste_registra(v, a, n, l, gen, true, idx)
static void prova_viste(void)
{
    PhViste *v = ph_viste_nuove();
    size_t idx = 0;
    unsigned gen = 1; /* generazione dei servizi */
    PhSrvNotifica ab[2] = {nota("a", 10), nota("b", 20)};

    /* La prima fotografia e' la base: niente toast per cio' che c'e' gia'. */
    CONTROLLA(!ph_viste_ha_base(v));
    CONTROLLA(REG(v, ab, 2, true, &idx) == 0);
    CONTROLLA(idx == SIZE_MAX);
    CONTROLLA(ph_viste_ha_base(v));
    CONTROLLA(REG(v, ab, 2, true, &idx) == 0);

    /* Una notifica nuova: una sola, con il suo indice. */
    PhSrvNotifica cab[3] = {nota("c", 30), ab[0], ab[1]};
    CONTROLLA(REG(v, cab, 3, true, &idx) == 1);
    CONTROLLA(idx == 0);

    /* Aggiornata (stessa chiave, ora diversa): non e' nuova. */
    PhSrvNotifica agg[3] = {nota("c", 30), nota("a", 99), ab[1]};
    CONTROLLA(REG(v, agg, 3, true, &idx) == 0);
    CONTROLLA(idx == SIZE_MAX);

    /* Due nuove insieme: il toast e' della piu' recente. */
    PhSrvNotifica due[5] = {nota("d", 50), nota("e", 90), agg[0], agg[1], agg[2]};
    CONTROLLA(REG(v, due, 5, true, &idx) == 2);
    CONTROLLA(idx == 1);

    /* Illeggibile: non cambia niente (le stesse non tornano "nuove"). */
    CONTROLLA(REG(v, NULL, 0, false, &idx) == 0);
    CONTROLLA(idx == SIZE_MAX);
    CONTROLLA(REG(v, due, 5, true, &idx) == 0);

    /* Sparita e tornata: e' di nuovo nuova. */
    PhSrvNotifica solo_a[1] = {nota("a", 99)};
    CONTROLLA(REG(v, solo_a, 1, true, &idx) == 0);
    PhSrvNotifica a_e_b[2] = {nota("a", 99), nota("b", 20)};
    CONTROLLA(REG(v, a_e_b, 2, true, &idx) == 1);
    CONTROLLA(idx == 1);

    /* Nuovo collegamento (generazione nuova): la prima fotografia e' di nuovo
     * la base, anche se arriva dopo una illeggibile. */
    gen = 2;
    CONTROLLA(REG(v, NULL, 0, false, &idx) == 0);
    CONTROLLA(!ph_viste_ha_base(v));
    PhSrvNotifica tre[3] = {nota("x", 1), nota("y", 2), nota("z", 3)};
    CONTROLLA(REG(v, tre, 3, true, &idx) == 0);
    CONTROLLA(ph_viste_ha_base(v));
    PhSrvNotifica quattro[4] = {nota("w", 4), tre[0], tre[1], tre[2]};
    CONTROLLA(REG(v, quattro, 4, true, &idx) == 1);

    /* Ritorno dopo una caduta: finche' la prima lettura non e' arrivata
     * (pronte = false) la fotografia e' quella di prima e non conta; la prima
     * con la generazione nuova, anche con una chiave in piu', e' la base. Il
     * profilo caricato nel mezzo non tocca le viste. */
    CONTROLLA(ph_viste_registra(v, quattro, 4, true, 2, false, &idx) == 0);
    PhSrvNotifica nuova_foto[5] = {nota("n", 8), quattro[0], quattro[1], quattro[2], quattro[3]};
    CONTROLLA(ph_viste_registra(v, nuova_foto, 5, true, 3, true, &idx) == 0);
    CONTROLLA(idx == SIZE_MAX);
    PhSrvNotifica ancora[6] = {nota("m", 9), nuova_foto[0], nuova_foto[1], nuova_foto[2], nuova_foto[3], nuova_foto[4]};
    CONTROLLA(ph_viste_registra(v, ancora, 6, true, 3, true, &idx) == 1);
    CONTROLLA(idx == 0);

    /* Prima lettura VUOTA, poi notifiche che arrivano dopo: sono nuove. */
    CONTROLLA(ph_viste_registra(v, NULL, 0, true, 4, true, &idx) == 0);
    CONTROLLA(ph_viste_ha_base(v));
    PhSrvNotifica una[1] = {nota("q", 20)};
    CONTROLLA(ph_viste_registra(v, una, 1, true, 4, true, &idx) == 1);
    CONTROLLA(idx == 0);
    /* Generazione 0 = nessuna prima lettura ancora: mai conta. */
    CONTROLLA(ph_viste_registra(v, una, 1, true, 0, true, &idx) == 0);

    /* Senza chiave: non rompe niente. */
    PhSrvNotifica nk = nota(NULL, 5);
    CONTROLLA(ph_viste_registra(v, &nk, 1, true, 4, true, &idx) == 1);
    CONTROLLA(ph_viste_registra(v, &nk, 1, true, 4, true, &idx) == 0);
    ph_viste_libera(v);
}

static void prova_ora(void)
{
    GDateTime *t = g_date_time_new_utc(2026, 11, 23, 14, 5, 0);
    CONTROLLA(t != NULL);
    uint64_t ms = (uint64_t)g_date_time_to_unix(t) * 1000 + 123;
    g_date_time_unref(t);
    char out[64];
    GTimeZone *utc = g_time_zone_new_utc();
    ph_sistema_ora_formatta(ms, utc, out, sizeof out);
    UGUALI(out, "14:05");
    GTimeZone *piu2 = g_time_zone_new_identifier("+02:00");
    if (piu2 != NULL) {
        ph_sistema_ora_formatta(ms, piu2, out, sizeof out);
        UGUALI(out, "16:05");
        g_time_zone_unref(piu2);
    }
    ph_sistema_ora_formatta(0, utc, out, sizeof out); /* ora ignota */
    UGUALI(out, "--:--");
    ph_sistema_ora_formatta(UINT64_MAX, utc, out, sizeof out);
    UGUALI(out, "--:--");
    ph_sistema_ora_formatta(ms, NULL, out, sizeof out);
    UGUALI(out, "--:--");
    char picc[4];
    ph_sistema_ora_formatta(ms, utc, picc, sizeof picc); /* buffer corto: mai fuori */
    CONTROLLA(strlen(picc) < sizeof picc);
    ph_sistema_ora_locale(ms, out, sizeof out);
    CONTROLLA(strlen(out) == 5 && out[2] == ':');

    ph_sistema_app_ora("Messaggi", ms, utc, out, sizeof out);
    UGUALI(out, "Messaggi \xC2\xB7 14:05");
    ph_sistema_app_ora("Mes\nsaggi\x01", ms, utc, out, sizeof out); /* nome ostile */
    UGUALI(out, "Mes saggi \xC2\xB7 14:05");
    ph_sistema_app_ora(NULL, ms, utc, out, sizeof out);
    UGUALI(out, " \xC2\xB7 14:05");
    g_time_zone_unref(utc);
}

static void prova_telefono(void)
{
    CONTROLLA(ph_sistema_versione_android(34) == 14);
    CONTROLLA(ph_sistema_versione_android(35) == 15);
    CONTROLLA(ph_sistema_versione_android(36) == 16);
    CONTROLLA(ph_sistema_versione_android(37) == 17);
    CONTROLLA(ph_sistema_versione_android(33) == 13);
    CONTROLLA(ph_sistema_versione_android(31) == 12);
    CONTROLLA(ph_sistema_versione_android(29) == 10);
    CONTROLLA(ph_sistema_versione_android(0) == 0);
    CONTROLLA(ph_sistema_versione_android(-7) == 0);

    char out[64];
    ph_sistema_batteria_pieno(0, out, sizeof out);
    UGUALI(out, "");
    ph_sistema_batteria_pieno(-3, out, sizeof out);
    UGUALI(out, "");
    ph_sistema_batteria_pieno(100, out, sizeof out);
    UGUALI(out, "M4 8H15.0V12H4z"); /* 11 px a partire da x=4 */
    ph_sistema_batteria_pieno(250, out, sizeof out);
    UGUALI(out, "M4 8H15.0V12H4z");
    ph_sistema_batteria_pieno(78, out, sizeof out);
    UGUALI(out, "M4 8H12.5V12H4z");
    ph_sistema_batteria_pieno(50, out, sizeof out);
    UGUALI(out, "M4 8H9.5V12H4z");
    ph_sistema_batteria_pieno(1, out, sizeof out);
    UGUALI(out, "M4 8H4.1V12H4z");
    char picc[6];
    ph_sistema_batteria_pieno(78, picc, sizeof picc);
    CONTROLLA(strlen(picc) < sizeof picc);
    CONTROLLA(ph_sistema_batteria_limita(-1) == 0 && ph_sistema_batteria_limita(101) == 100 &&
              ph_sistema_batteria_limita(55) == 55);

    PhSrvCollegamento c = {0};
    PhSrvTelefono t = {0};
    char riga[256];
    ph_sistema_riga_telefono(&c, &t, riga, sizeof riga); /* nessun telefono */
    UGUALI(riga, "");
    c.ha_telefono = true;
    c.api = 34;
    ph_sistema_riga_telefono(&c, &t, riga, sizeof riga); /* stato non ancora noto */
    UGUALI(riga, "Android 14");
    t.noto = true;
    t.batteria = 78;
    t.rete = PH_SRV_RETE_WIFI;
    ph_sistema_riga_telefono(&c, &t, riga, sizeof riga);
    UGUALI(riga, "Android 14 \xC2\xB7 Batteria 78% \xC2\xB7 Wi-Fi connesso");
    t.rete = PH_SRV_RETE_CELLULARE;
    ph_sistema_riga_telefono(&c, &t, riga, sizeof riga);
    UGUALI(riga, "Android 14 \xC2\xB7 Batteria 78% \xC2\xB7 Rete cellulare");
    t.rete = PH_SRV_RETE_NESSUNA;
    c.api = 36;
    t.batteria = 250;
    ph_sistema_riga_telefono(&c, &t, riga, sizeof riga);
    UGUALI(riga, "Android 16 \xC2\xB7 Batteria 100% \xC2\xB7 Nessuna rete");
    c.api = 0;
    ph_sistema_riga_telefono(&c, &t, riga, sizeof riga);
    CONTROLLA(strncmp(riga, "Android ? ", 10) == 0);
    char piccola[8];
    ph_sistema_riga_telefono(&c, &t, piccola, sizeof piccola);
    CONTROLLA(strlen(piccola) < sizeof piccola);

    ph_lingua_imposta(PH_LINGUA_EN);
    c.api = 34;
    t.batteria = 20;
    t.rete = PH_SRV_RETE_WIFI;
    ph_sistema_riga_telefono(&c, &t, riga, sizeof riga);
    UGUALI(riga, "Android 14 \xC2\xB7 Battery 20% \xC2\xB7 Wi-Fi connected");
    ph_lingua_imposta(PH_LINGUA_IT);
}

static void prova_pannello(void)
{
    PhPannelloAttesa a = {0};
    ph_pannello_richiedi(&a, true, 1);
    ph_pannello_annulla(&a); /* collegamento caduto: attesa chiusa, nessun esito */
    CONTROLLA(!a.in_attesa);
    CONTROLLA(ph_pannello_controlla(&a, false, 1 + 10 * PH_PANNELLO_ATTESA_MS) == PH_PANNELLO_NIENTE);
    CONTROLLA(ph_pannello_controlla(&a, true, 5000) == PH_PANNELLO_NIENTE); /* nessuna richiesta */

    /* Spegnimento riuscito: lo stato vero diventa "spento". */
    ph_pannello_richiedi(&a, false, 1000);
    CONTROLLA(a.in_attesa);
    CONTROLLA(ph_pannello_controlla(&a, true, 1500) == PH_PANNELLO_NIENTE); /* ancora acceso: si aspetta */
    CONTROLLA(ph_pannello_controlla(&a, false, 2000) == PH_PANNELLO_FATTO);
    CONTROLLA(!a.in_attesa);

    /* Rifiutato (bloccato, chiamata, ripristino non sicuro): lo stato non cambia. */
    ph_pannello_richiedi(&a, false, 1000);
    CONTROLLA(ph_pannello_controlla(&a, true, 1000 + PH_PANNELLO_ATTESA_MS - 1) == PH_PANNELLO_NIENTE);
    CONTROLLA(ph_pannello_controlla(&a, true, 1000 + PH_PANNELLO_ATTESA_MS) == PH_PANNELLO_RIFIUTATO);
    CONTROLLA(!a.in_attesa);
    /* L'evento puo' arrivare anche dopo: nessun secondo messaggio. */
    CONTROLLA(ph_pannello_controlla(&a, false, 1000 + PH_PANNELLO_ATTESA_MS + 10) == PH_PANNELLO_NIENTE);

    /* Lo stato vero arriva prima di ogni altra cosa, e allo scadere vince lo stato. */
    ph_pannello_richiedi(&a, true, 100);
    CONTROLLA(ph_pannello_controlla(&a, true, 100 + PH_PANNELLO_ATTESA_MS) == PH_PANNELLO_FATTO);
    ph_pannello_richiedi(&a, true, 100);
    CONTROLLA(ph_pannello_controlla(&a, false, 100 + PH_PANNELLO_ATTESA_MS + 1) == PH_PANNELLO_RIFIUTATO);
}

static void prova_volume(void)
{
    char out[64];
    CONTROLLA(ph_sistema_volume_limita(-5) == 0 && ph_sistema_volume_limita(150) == 100 &&
              ph_sistema_volume_limita(42) == 42);
    CONTROLLA(ph_sistema_volume_muto_mostrato(0, false));
    CONTROLLA(ph_sistema_volume_muto_mostrato(50, true));
    CONTROLLA(!ph_sistema_volume_muto_mostrato(50, false));
    CONTROLLA(ph_sistema_volume_muto_mostrato(-3, false)); /* fuori campo = 0 */

    ph_sistema_volume_testo(60, false, out, sizeof out);
    UGUALI(out, "60%");
    ph_sistema_volume_testo(60, true, out, sizeof out);
    UGUALI(out, "Muto");
    ph_sistema_volume_testo(0, false, out, sizeof out);
    UGUALI(out, "Muto");
    ph_sistema_volume_testo(180, false, out, sizeof out);
    UGUALI(out, "100%");
    ph_sistema_volume_etichetta(60, false, out, sizeof out);
    UGUALI(out, "Volume: 60%");
    ph_sistema_volume_etichetta(60, true, out, sizeof out);
    UGUALI(out, "Volume: muto");
    ph_sistema_volume_etichetta(0, false, out, sizeof out);
    UGUALI(out, "Volume: muto");
    char picc[5];
    ph_sistema_volume_etichetta(60, false, picc, sizeof picc);
    CONTROLLA(strlen(picc) < sizeof picc);

    /* Il livello mosso e' subito nel profilo (e solo se cambia serve salvare). */
    PhCfgTelefono *prof = ph_cfg_telefono_nuovo();
    CONTROLLA(prof != NULL);
    if (prof != NULL) {
        prof->volume = 60;
        CONTROLLA(!ph_sistema_volume_nel_profilo(prof, 60));
        CONTROLLA(ph_sistema_volume_nel_profilo(prof, 35) && prof->volume == 35);
        CONTROLLA(ph_sistema_volume_nel_profilo(prof, 250) && prof->volume == 100);
        CONTROLLA(ph_sistema_volume_nel_profilo(prof, -4) && prof->volume == 0);
        ph_cfg_telefono_libera(prof);
    }

    ph_lingua_imposta(PH_LINGUA_EN);
    ph_sistema_volume_testo(0, false, out, sizeof out);
    UGUALI(out, "Mute");
    ph_sistema_volume_etichetta(0, false, out, sizeof out);
    UGUALI(out, "Volume: muted");
    ph_lingua_imposta(PH_LINGUA_IT);
}

/* Il CSS del pezzo non scrive colori a mano: stanno su vetro chiaro o scuro, quindi solo
 * variabili --ph-*. Rilevatore comune (shell/prova_css.h). */
static void prova_css_senza_colori(void)
{
    CONTROLLA(ph_prova_css_risorsa("/it/yasde/shell/sistema.css", "sistema.css", true) == 0);
    CONTROLLA(ph_prova_css_autoprova() == 0);
}

int main(void)
{
    ph_lingua_imposta(PH_LINGUA_IT);
    ph_risorse_sistema_register_resource();
    if (ph_shell_stile_controlla("/it/yasde/shell/sistema.css") != 0) {
        fprintf(stderr, "sistema.css: errori di sintassi\n");
        errori++;
    }
    prova_css_senza_colori();
    prova_testo_sicuro();
    prova_nascoste();
    prova_viste();
    prova_ora();
    prova_telefono();
    prova_pannello();
    prova_volume();
    if (errori == 0)
        printf("prova_sistema: tutto bene\n");
    return errori == 0 ? 0 : 1;
}
