/* Prove di logica di «Batteria in dettaglio» (tonda) e «Prossima sveglia» (capsula), passo 2b:
 * SENZA schermo. Ogni prova fallisce se si toglie il codice che prova (valori e confini scelti
 * perche' una soglia sbagliata o una formula storta li faccia cadere). */
#include <glib.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config/lingua.h"
#include "logica_telefono.h"

static int errori = 0;
#define VERIFICA(cond)                                                                       \
    do {                                                                                     \
        if (!(cond)) {                                                                       \
            fprintf(stderr, "%s:%d: fallita: %s\n", __FILE__, __LINE__, #cond);              \
            errori++;                                                                        \
        }                                                                                    \
    } while (0)
#define VERIFICA_STR(a, b)                                                                   \
    do {                                                                                     \
        if (strcmp((a), (b)) != 0) {                                                         \
            fprintf(stderr, "%s:%d: \"%s\" != \"%s\"\n", __FILE__, __LINE__, (a), (b));      \
            errori++;                                                                        \
        }                                                                                    \
    } while (0)

static uint64_t ms_utc(int a, int m, int g, int h, int mi)
{
    GDateTime *dt = g_date_time_new_utc(a, m, g, h, mi, 0);
    uint64_t ms = (uint64_t)g_date_time_to_unix(dt) * 1000u;
    g_date_time_unref(dt);
    return ms;
}

/* ---- Batteria ---------------------------------------------------------------------------------- */
static void prova_bat_colore(void)
{
    VERIFICA_STR(PH_DSK_BAT_PACCHETTO_IMPOSTAZIONI, "com.android.settings");
    VERIFICA(ph_dsk_bat_bassa(20) && ph_dsk_bat_bassa(0) && ph_dsk_bat_bassa(-5));
    VERIFICA(!ph_dsk_bat_bassa(21) && !ph_dsk_bat_bassa(100));
    VERIFICA(ph_dsk_bat_accento(15, false) == PH_DSK_ACC_CORALLO);
    VERIFICA(ph_dsk_bat_accento(20, false) == PH_DSK_ACC_CORALLO);
    VERIFICA(ph_dsk_bat_accento(21, false) == PH_DSK_ACC_VERDE);
    VERIFICA(ph_dsk_bat_accento(15, true) == PH_DSK_ACC_VERDE); /* in carica sale: resta verde */
    VERIFICA(ph_dsk_bat_accento(100, false) == PH_DSK_ACC_VERDE);
}

static void prova_bat_noti(void)
{
    bool n[PH_DSK_BAT_G_N];
    PhSrvBatteria b;
    memset(&b, 0, sizeof b);
    ph_dsk_bat_gettoni_noti(&b, n);
    VERIFICA(!n[0] && !n[1] && !n[2]); /* niente dato: niente gettoni inventati */
    ph_dsk_bat_gettoni_noti(NULL, n);
    VERIFICA(!n[0] && !n[1] && !n[2]);
    b.temperatura_nota = true;
    b.temperatura_decimi = 315;
    b.salute = PH_SRV_SALUTE_BUONA;
    b.tensione_mv = 4120;
    ph_dsk_bat_gettoni_noti(&b, n);
    VERIFICA(n[0] && n[1] && n[2]);
    /* Dati ostili: fuori da un intervallo plausibile non si mostrano. */
    b.temperatura_decimi = 9999;
    b.tensione_mv = 99999;
    b.salute = (PhSrvSalute)77;
    ph_dsk_bat_gettoni_noti(&b, n);
    VERIFICA(!n[0] && !n[1] && !n[2]);
    b.temperatura_decimi = -501;
    b.tensione_mv = -3;
    b.salute = PH_SRV_SALUTE_SCONOSCIUTA;
    ph_dsk_bat_gettoni_noti(&b, n);
    VERIFICA(!n[0] && !n[1] && !n[2]);
    b.temperatura_nota = false;
    b.temperatura_decimi = 300;
    ph_dsk_bat_gettoni_noti(&b, n);
    VERIFICA(!n[0]);
}

static void prova_bat_disposizione(void)
{
    const bool tutti[PH_DSK_BAT_G_N] = {true, true, true};
    const bool nessuno[PH_DSK_BAT_G_N] = {false, false, false};
    const bool solo_tensione[PH_DSK_BAT_G_N] = {false, false, true};
    PhDskBatDisp d;

    /* Minimo (136): quadrato inscritto 97, stato 20, anello 77; nessun posto per i gettoni. */
    d = ph_dsk_bat_disposizione(136, 136, tutti);
    VERIFICA(d.stato && d.lato_anello == 77 && !d.gettone[0] && !d.gettone[1] && !d.gettone[2]);
    /* 176: un gettone (la temperatura, la prima in ordine), anello 84. */
    d = ph_dsk_bat_disposizione(176, 176, tutti);
    VERIFICA(d.stato && d.gettone[PH_DSK_BAT_G_TEMPERATURA] && !d.gettone[1] && !d.gettone[2] && d.lato_anello == 84);
    /* 216: temperatura e salute, anello 91. */
    d = ph_dsk_bat_disposizione(216, 216, tutti);
    VERIFICA(d.gettone[0] && d.gettone[1] && !d.gettone[2] && d.lato_anello == 91);
    /* 320: tutti e tre, anello 144. */
    d = ph_dsk_bat_disposizione(320, 320, tutti);
    VERIFICA(d.gettone[0] && d.gettone[1] && d.gettone[2] && d.lato_anello == 144);
    /* Un gettone solo noto: prende il posto anche se non e' il primo; cio' che avanza va all'anello. */
    d = ph_dsk_bat_disposizione(176, 176, solo_tensione);
    VERIFICA(d.gettone[2] && !d.gettone[0] && d.lato_anello == 84);
    d = ph_dsk_bat_disposizione(216, 216, solo_tensione);
    VERIFICA(d.gettone[2] && d.lato_anello == 113); /* 155 - 20 - 22 */
    /* Nessuna misura nota: solo anello e stato; l'anello prende tutto il resto. */
    d = ph_dsk_bat_disposizione(320, 320, nessuno);
    VERIFICA(d.stato && !d.gettone[0] && !d.gettone[1] && !d.gettone[2] && d.lato_anello == 230 - 20);
    /* Conta il lato corto, non la larghezza. */
    d = ph_dsk_bat_disposizione(400, 136, tutti);
    VERIFICA(d.lato_anello == 77 && !d.gettone[0]);
    d = ph_dsk_bat_disposizione(136, 400, tutti);
    VERIFICA(d.lato_anello == 77 && !d.gettone[0]);
    /* Il testo non va oltre il quadrato inscritto: larghezza utile e tetto ai caratteri. */
    d = ph_dsk_bat_disposizione(176, 176, tutti);
    VERIFICA(d.larghezza_utile == 126 && d.caratteri_max == 15);
    d = ph_dsk_bat_disposizione(320, 320, tutti);
    VERIFICA(d.larghezza_utile == 230 && d.caratteri_max == 28);
    d = ph_dsk_bat_disposizione(40, 40, tutti);
    VERIFICA(d.larghezza_utile == 28 && d.caratteri_max == 4);
    d = ph_dsk_bat_disposizione(0, 0, tutti);
    VERIFICA(d.larghezza_utile == 0 && d.caratteri_max == 4);
    /* Troppo piccolo per lo stato: solo l'anello. */
    d = ph_dsk_bat_disposizione(100, 100, tutti);
    VERIFICA(!d.stato && d.lato_anello == 72 && !d.gettone[0]);
    /* Misure assurde. */
    d = ph_dsk_bat_disposizione(0, 0, tutti);
    VERIFICA(d.lato_anello == 0 && !d.stato && !d.gettone[0]);
    d = ph_dsk_bat_disposizione(-10, 300, tutti);
    VERIFICA(d.lato_anello == 0 && !d.stato);
    d = ph_dsk_bat_disposizione(INT_MAX, INT_MAX, tutti);
    VERIFICA(d.lato_anello > 0 && d.lato_anello <= 4096 && d.gettone[0] && d.gettone[1] && d.gettone[2]);
    d = ph_dsk_bat_disposizione(300, 300, NULL);
    VERIFICA(d.stato && !d.gettone[0]);
}

static void prova_bat_testi(void)
{
    char o[96];
    PhSrvBatteria b;
    memset(&b, 0, sizeof b);
    b.stato = PH_SRV_BAT_IN_CARICA;
    ph_dsk_bat_stato_testo(PH_DSK_DATO_OK, &b, o, sizeof o);
    VERIFICA_STR(o, "In carica");
    b.stato = PH_SRV_BAT_IN_SCARICA;
    ph_dsk_bat_stato_testo(PH_DSK_DATO_OK, &b, o, sizeof o);
    VERIFICA_STR(o, "In scarica");
    b.stato = PH_SRV_BAT_NON_IN_CARICA;
    ph_dsk_bat_stato_testo(PH_DSK_DATO_OK, &b, o, sizeof o);
    VERIFICA_STR(o, "Non in carica");
    b.stato = PH_SRV_BAT_PIENA;
    ph_dsk_bat_stato_testo(PH_DSK_DATO_OK, &b, o, sizeof o);
    VERIFICA_STR(o, "Carica completa");
    b.stato = PH_SRV_BAT_SCONOSCIUTO;
    ph_dsk_bat_stato_testo(PH_DSK_DATO_OK, &b, o, sizeof o);
    VERIFICA_STR(o, "");
    ph_dsk_bat_stato_testo(PH_DSK_DATO_ATTESA, &b, o, sizeof o);
    VERIFICA_STR(o, "");
    ph_dsk_bat_stato_testo(PH_DSK_DATO_NON_DISPONIBILE, NULL, o, sizeof o);
    VERIFICA_STR(o, "Non disponibile");
    ph_dsk_bat_stato_testo(PH_DSK_DATO_ERRORE, NULL, o, sizeof o);
    VERIFICA_STR(o, "Errore");
    ph_dsk_bat_stato_testo(PH_DSK_DATO_OK, NULL, o, sizeof o);
    VERIFICA_STR(o, "");

    b.temperatura_nota = true;
    b.temperatura_decimi = 315;
    b.salute = PH_SRV_SALUTE_BUONA;
    b.tensione_mv = 4120;
    ph_dsk_bat_gettone_testo(PH_DSK_BAT_G_TEMPERATURA, &b, o, sizeof o);
    VERIFICA_STR(o, "31,5 °C");
    ph_dsk_bat_gettone_testo(PH_DSK_BAT_G_SALUTE, &b, o, sizeof o);
    VERIFICA_STR(o, "Salute: Buona");
    ph_dsk_bat_gettone_testo(PH_DSK_BAT_G_TENSIONE, &b, o, sizeof o);
    VERIFICA_STR(o, "4,12 V");
    b.tensione_mv = 0;
    ph_dsk_bat_gettone_testo(PH_DSK_BAT_G_TENSIONE, &b, o, sizeof o);
    VERIFICA_STR(o, ""); /* non nota: nessun testo inventato */
    ph_dsk_bat_gettone_testo(7, &b, o, sizeof o);
    VERIFICA_STR(o, "");
    ph_dsk_bat_gettone_testo(PH_DSK_BAT_G_TEMPERATURA, NULL, o, sizeof o);
    VERIFICA_STR(o, "");
    ph_dsk_bat_gettone_testo(PH_DSK_BAT_G_TEMPERATURA, &b, o, 0); /* cap 0: non scrive */
    char piccolo[4];
    ph_dsk_bat_gettone_testo(PH_DSK_BAT_G_TEMPERATURA, &b, piccolo, sizeof piccolo);
    VERIFICA(strlen(piccolo) < sizeof piccolo);
}

/* ---- Sveglia ------------------------------------------------------------------------------------- */
static void prova_sve_tempo(void)
{
    char o[96];
    uint64_t adesso = ms_utc(2026, 11, 23, 10, 0);

    /* Meno di un minuto: 0 minuti, testo dedicato. */
    VERIFICA(ph_dsk_sve_minuti(adesso + 30000, adesso) == 0);
    VERIFICA(!ph_dsk_sve_passata(adesso + 30000, adesso));
    ph_dsk_sve_fra(adesso + 30000, adesso, o, sizeof o);
    VERIFICA_STR(o, "fra meno di un minuto");
    VERIFICA(ph_dsk_sve_minuti(adesso + 59999, adesso) == 0 && ph_dsk_sve_minuti(adesso + 60000, adesso) == 1);
    /* Ore e minuti. */
    ph_dsk_sve_fra(adesso + 90 * 60000, adesso, o, sizeof o);
    VERIFICA_STR(o, "fra 1 h 30 min");
    ph_dsk_sve_fra(adesso + 7 * 3600000ull, adesso, o, sizeof o);
    VERIFICA_STR(o, "fra 7 ore");
    ph_dsk_sve_fra(adesso + 25 * 60000, adesso, o, sizeof o);
    VERIFICA_STR(o, "fra 25 minuti");
    /* Piu' di un giorno. */
    ph_dsk_sve_fra(adesso + 25 * 3600000ull, adesso, o, sizeof o);
    VERIFICA_STR(o, "fra un giorno");
    ph_dsk_sve_fra(adesso + 49 * 3600000ull, adesso, o, sizeof o);
    VERIFICA_STR(o, "fra 2 giorni");
    /* Gia' passata (o esattamente adesso): mai negativo, nessun «fra». */
    VERIFICA(ph_dsk_sve_passata(adesso - 3600000, adesso) && ph_dsk_sve_passata(adesso, adesso));
    VERIFICA(ph_dsk_sve_minuti(adesso - 3600000, adesso) == 0);
    VERIFICA(ph_dsk_sve_minuti(0, UINT64_MAX) == 0);
    ph_dsk_sve_fra(adesso - 3600000, adesso, o, sizeof o);
    VERIFICA_STR(o, "");
    /* Istanti assurdi: nessun crollo. */
    VERIFICA(ph_dsk_sve_minuti(UINT64_MAX, 0) > 0);
    ph_dsk_sve_fra(UINT64_MAX, 0, o, sizeof o);
    VERIFICA(o[0] != '\0');
    ph_dsk_sve_fra(adesso + 60000, adesso, o, 0); /* cap 0: non scrive */

    /* Cambio dell'ora legale (Roma): il tempo che manca e' quello vero, non la differenza degli
     * orologi a muro. 24 ott 22:00 (CEST) -> 25 ott 07:00 (CET): in mezzo c'e' un'ora in piu', 10 ore. */
    GTimeZone *roma = g_time_zone_new_identifier("Europe/Rome");
    VERIFICA(roma != NULL);
    if (roma != NULL) {
        uint64_t sera = ms_utc(2026, 10, 24, 20, 0), mattina = ms_utc(2026, 10, 25, 6, 0);
        VERIFICA(ph_dsk_sve_minuti(mattina, sera) == 600);
        ph_dsk_sve_fra(mattina, sera, o, sizeof o);
        VERIFICA_STR(o, "fra 10 ore");
        ph_dsk_sve_ora_testo(mattina, roma, o, sizeof o);
        VERIFICA_STR(o, "07:00");
        /* In primavera: 28 mar 22:00 (CET) -> 29 mar 07:00 (CEST): un'ora in meno, 8 ore. */
        sera = ms_utc(2026, 3, 28, 21, 0);
        mattina = ms_utc(2026, 3, 29, 5, 0);
        ph_dsk_sve_fra(mattina, sera, o, sizeof o);
        VERIFICA_STR(o, "fra 8 ore");
        ph_dsk_sve_ora_testo(mattina, roma, o, sizeof o);
        VERIFICA_STR(o, "07:00");
        g_time_zone_unref(roma);
    }
    ph_dsk_sve_ora_testo(UINT64_MAX, NULL, o, sizeof o);
    VERIFICA_STR(o, "");
}

static void prova_sve_testi(void)
{
    char o[160];
    ph_dsk_sve_assente_testo(PH_DSK_DATO_OK, false, o, sizeof o);
    VERIFICA_STR(o, "Nessuna sveglia");
    ph_dsk_sve_assente_testo(PH_DSK_DATO_OK, true, o, sizeof o);
    VERIFICA_STR(o, "");
    ph_dsk_sve_assente_testo(PH_DSK_DATO_ATTESA, false, o, sizeof o);
    VERIFICA_STR(o, ""); /* non ancora arrivato: non si dice «nessuna sveglia» */
    ph_dsk_sve_assente_testo(PH_DSK_DATO_NON_DISPONIBILE, false, o, sizeof o);
    VERIFICA_STR(o, "Non disponibile");
    ph_dsk_sve_assente_testo(PH_DSK_DATO_ERRORE, false, o, sizeof o);
    VERIFICA_STR(o, "Errore");
    ph_dsk_sve_giorno_fra("Domani", "fra 7 ore", o, sizeof o);
    VERIFICA_STR(o, "Domani · fra 7 ore");
    ph_dsk_sve_giorno_fra("Domani", "", o, sizeof o);
    VERIFICA_STR(o, "Domani");
    ph_dsk_sve_giorno_fra(NULL, NULL, o, sizeof o);
    VERIFICA_STR(o, "");
}

static void prova_sve_disposizione(void)
{
    PhDskSveDisp d;
    /* Il tondo e' un cerchio: il minore tra larghezza e altezza, tetto 72, sotto 24 niente. */
    VERIFICA(ph_dsk_sve_tondo_lato(400, 100) == 72 && ph_dsk_sve_tondo_lato(400, 50) == 50);
    VERIFICA(ph_dsk_sve_tondo_lato(40, 100) == 40 && ph_dsk_sve_tondo_lato(72, 72) == 72);
    VERIFICA(ph_dsk_sve_tondo_lato(73, 500) == 72 && ph_dsk_sve_tondo_lato(100, 23) == 0);
    VERIFICA(ph_dsk_sve_tondo_lato(0, 50) == 0 && ph_dsk_sve_tondo_lato(-5, -5) == 0);
    VERIFICA(ph_dsk_sve_tondo_lato(INT_MAX, INT_MAX) == 72);
    /* In una capsula larga e alta (480 x 128) il tondo non si allunga: lato 72, non l'altezza. */
    d = ph_dsk_sve_disposizione(480, 128, true);
    VERIFICA(d.tondo && d.tondo_lato == 72);
    /* Minimo (200 x 72): tondo di 52, una riga sotto (il giorno), il «fra» non ci sta. */
    d = ph_dsk_sve_disposizione(200, 72, true);
    VERIFICA(d.tondo && d.tondo_lato == 52 && d.giorno && !d.fra && !d.unite && d.testo_larghezza == 88);
    VERIFICA(d.ora_px >= 14 && d.ora_px <= 40);
    /* Largo: giorno e «fra» sulla stessa riga. */
    d = ph_dsk_sve_disposizione(330, 72, true);
    VERIFICA(d.giorno && d.fra && d.unite);
    /* Alto: due righe sotto l'ora. */
    d = ph_dsk_sve_disposizione(320, 104, true);
    VERIFICA(d.giorno && d.fra && !d.unite && d.tondo_lato == 72 && d.ora_px == 40);
    /* Stretto: niente tondo. */
    d = ph_dsk_sve_disposizione(130, 72, true);
    VERIFICA(!d.tondo && d.tondo_lato == 0 && d.testo_larghezza == 82);
    /* Confini: il tondo serve 150 px di larghezza utile (198 di scheda); la seconda riga 78 px di
     * altezza utile (98 di scheda); il «fra» sulla stessa riga 190 px di testo. */
    d = ph_dsk_sve_disposizione(197, 72, true);
    VERIFICA(!d.tondo);
    d = ph_dsk_sve_disposizione(198, 72, true);
    VERIFICA(d.tondo);
    VERIFICA(ph_dsk_sve_disposizione(250, 97, true).giorno && !ph_dsk_sve_disposizione(250, 97, true).fra);
    VERIFICA(ph_dsk_sve_disposizione(250, 98, true).fra && !ph_dsk_sve_disposizione(250, 98, true).unite);
    VERIFICA(!ph_dsk_sve_disposizione(250, 43 + 20, true).giorno && ph_dsk_sve_disposizione(250, 44 + 20, true).giorno);
    VERIFICA(!ph_dsk_sve_disposizione(301, 72, true).unite && ph_dsk_sve_disposizione(302, 72, true).unite);
    /* Senza sveglia conta solo il tondo. */
    d = ph_dsk_sve_disposizione(320, 104, false);
    VERIFICA(d.tondo && !d.giorno && !d.fra && !d.unite);
    /* Basso: solo l'ora. */
    d = ph_dsk_sve_disposizione(320, 60, true);
    VERIFICA(!d.giorno && !d.fra);
    /* Misure assurde. */
    d = ph_dsk_sve_disposizione(0, 0, true);
    VERIFICA(!d.tondo && !d.giorno && !d.fra && d.ora_px == 0 && d.testo_larghezza == 0);
    d = ph_dsk_sve_disposizione(-5, 100, true);
    VERIFICA(!d.tondo && !d.giorno);
    d = ph_dsk_sve_disposizione(30, 30, true);
    VERIFICA(!d.tondo && !d.giorno);
    d = ph_dsk_sve_disposizione(INT_MAX, INT_MAX, true);
    VERIFICA(d.tondo && d.tondo_lato == 72 && d.giorno && d.fra && d.ora_px <= 40);
}

int main(void)
{
    ph_lingua_imposta(PH_LINGUA_IT);
    prova_bat_colore();
    prova_bat_noti();
    prova_bat_disposizione();
    prova_bat_testi();
    prova_sve_tempo();
    prova_sve_testi();
    prova_sve_disposizione();
    if (errori != 0) {
        fprintf(stderr, "prova_telefono: %d verifiche fallite\n", errori);
        return 1;
    }
    printf("prova_telefono: ok\n");
    return 0;
}
