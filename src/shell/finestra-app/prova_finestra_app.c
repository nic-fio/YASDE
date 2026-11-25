/* Prove di logica di shell-finestra-app (F-05), SENZA schermo: tasti, chiavi
 * corte, misure iniziali, nome dello screenshot, passi di pausa, geometria del
 * titolo e, con numeri veri e scala frazionaria, la traduzione dell'input
 * attraverso finestre.h (le regole sono di finestre.h: qui si prova che il
 * modo in cui le usiamo porti il tocco nel punto giusto).
 * Gli accessi a widget (cornice, gesti) si vedono a occhio sul tablet. */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "finestre/finestre.h"
#include "logica.h"
#include "shell/prova_css.h"
#include "shell/shell.h"

void ph_risorse_finestra_app_register_resource(void);

static int errori;

/* y del bordo alto dei pulsanti dentro il titolo (centrati in verticale). */
#define PUL_Y ((PH_FIN_TITOLO_H - PH_FIN_PULS) / 2)

#define CHECK(cond)                                                                            \
    do {                                                                                       \
        if (!(cond)) {                                                                         \
            fprintf(stderr, "%s:%d: FALLITO: %s\n", __FILE__, __LINE__, #cond);               \
            errori++;                                                                          \
        }                                                                                      \
    } while (0)

/* ---- Tasti ---- */
static void prova_tasti(void)
{
    CHECK(ph_fa_tasto_android(GDK_KEY_a) == 29);
    CHECK(ph_fa_tasto_android(GDK_KEY_A) == 29);
    CHECK(ph_fa_tasto_android(GDK_KEY_z) == 54);
    CHECK(ph_fa_tasto_android(GDK_KEY_0) == 7);
    CHECK(ph_fa_tasto_android(GDK_KEY_9) == 16);
    CHECK(ph_fa_tasto_android(GDK_KEY_KP_0) == 144);
    CHECK(ph_fa_tasto_android(GDK_KEY_Return) == 66);
    CHECK(ph_fa_tasto_android(GDK_KEY_KP_Enter) == 66);
    CHECK(ph_fa_tasto_android(GDK_KEY_BackSpace) == 67);
    CHECK(ph_fa_tasto_android(GDK_KEY_Tab) == 61);
    CHECK(ph_fa_tasto_android(GDK_KEY_Escape) == 111);
    CHECK(ph_fa_tasto_android(GDK_KEY_Up) == 19);
    CHECK(ph_fa_tasto_android(GDK_KEY_Down) == 20);
    CHECK(ph_fa_tasto_android(GDK_KEY_Left) == 21);
    CHECK(ph_fa_tasto_android(GDK_KEY_Right) == 22);
    CHECK(ph_fa_tasto_android(GDK_KEY_space) == 62);
    CHECK(ph_fa_tasto_android(GDK_KEY_Delete) == 112);
    CHECK(ph_fa_tasto_android(GDK_KEY_Home) == 122);
    CHECK(ph_fa_tasto_android(GDK_KEY_End) == 123);
    CHECK(ph_fa_tasto_android(GDK_KEY_Page_Up) == 92);
    CHECK(ph_fa_tasto_android(GDK_KEY_Page_Down) == 93);
    CHECK(ph_fa_tasto_android(GDK_KEY_F1) == 131);
    CHECK(ph_fa_tasto_android(GDK_KEY_F12) == 142);
    CHECK(ph_fa_tasto_android(GDK_KEY_Shift_L) == 0);
    CHECK(ph_fa_tasto_android(GDK_KEY_eacute) == 0);

    CHECK(ph_fa_meta_android(0) == 0);
    CHECK(ph_fa_meta_android(GDK_SHIFT_MASK) == 1);
    CHECK(ph_fa_meta_android(GDK_ALT_MASK) == 2);
    CHECK(ph_fa_meta_android(GDK_CONTROL_MASK) == 0x1000);
    CHECK(ph_fa_meta_android(GDK_SHIFT_MASK | GDK_CONTROL_MASK | GDK_ALT_MASK) == 0x1003);
    /* Maiuscolo bloccato e tasti del mouse non sono meta di Android. */
    CHECK(ph_fa_meta_android(GDK_LOCK_MASK | GDK_BUTTON1_MASK) == 0);

    /* Pulsanti del mouse -> bit di TOCCO; indietro, avanti e altri non vanno al telefono. */
    CHECK(ph_fa_pulsante_tocco(GDK_BUTTON_PRIMARY) == 1);
    CHECK(ph_fa_pulsante_tocco(GDK_BUTTON_SECONDARY) == 2);
    CHECK(ph_fa_pulsante_tocco(GDK_BUTTON_MIDDLE) == 4);
    CHECK(ph_fa_pulsante_tocco(0) == 0);
    CHECK(ph_fa_pulsante_tocco(8) == 0);
    CHECK(ph_fa_pulsante_tocco(9) == 0);

    /* Testo o tasto. */
    CHECK(ph_fa_tasto_e_testo(GDK_KEY_a, 0));
    CHECK(ph_fa_tasto_e_testo(GDK_KEY_A, GDK_SHIFT_MASK));
    CHECK(ph_fa_tasto_e_testo(GDK_KEY_eacute, 0));
    CHECK(!ph_fa_tasto_e_testo(GDK_KEY_a, GDK_CONTROL_MASK));
    CHECK(!ph_fa_tasto_e_testo(GDK_KEY_a, GDK_ALT_MASK));
    CHECK(!ph_fa_tasto_e_testo(GDK_KEY_Return, 0));
    CHECK(!ph_fa_tasto_e_testo(GDK_KEY_Escape, 0));
    CHECK(!ph_fa_tasto_e_testo(GDK_KEY_Left, 0));
    CHECK(!ph_fa_tasto_e_testo(GDK_KEY_space, 0));

    /* Riservati alla shell. */
    CHECK(ph_fa_tasto_riservato(GDK_KEY_r, GDK_CONTROL_MASK) == PH_FA_RISERVATO_RUOTA);
    CHECK(ph_fa_tasto_riservato(GDK_KEY_R, GDK_CONTROL_MASK) == PH_FA_RISERVATO_RUOTA);
    CHECK(ph_fa_tasto_riservato(GDK_KEY_w, GDK_CONTROL_MASK) == PH_FA_RISERVATO_CHIUDI_APP);
    CHECK(ph_fa_tasto_riservato(GDK_KEY_C, GDK_CONTROL_MASK | GDK_SHIFT_MASK) ==
          PH_FA_RISERVATO_COPIA_SCREENSHOT);
    CHECK(ph_fa_tasto_riservato(GDK_KEY_F11, 0) == PH_FA_RISERVATO_SCHERMO_INTERO);
    /* Ctrl+C da solo e' del telefono (copia), Ctrl+Maiusc+R e Ctrl+Alt+W no. */
    CHECK(ph_fa_tasto_riservato(GDK_KEY_c, GDK_CONTROL_MASK) == PH_FA_RISERVATO_NESSUNO);
    CHECK(ph_fa_tasto_riservato(GDK_KEY_R, GDK_CONTROL_MASK | GDK_SHIFT_MASK) ==
          PH_FA_RISERVATO_NESSUNO);
    CHECK(ph_fa_tasto_riservato(GDK_KEY_w, GDK_CONTROL_MASK | GDK_ALT_MASK) ==
          PH_FA_RISERVATO_NESSUNO);
    CHECK(ph_fa_tasto_riservato(GDK_KEY_r, 0) == PH_FA_RISERVATO_NESSUNO);
}

/* ---- Chiavi corte ---- */
static void prova_chiavi(void)
{
    PhFaChiavi *t = ph_fa_chiavi_nuove();
    char k1[PH_FIN_APP_MAX], k2[PH_FIN_APP_MAX], k3[PH_FIN_APP_MAX], k4[PH_FIN_APP_MAX];

    CHECK(ph_fa_chiave_di(t, "it.finto.note", k1));
    CHECK(strcmp(k1, "a1") == 0);
    CHECK(ph_fa_chiave_di(t, "it.finto.note", k2));
    CHECK(strcmp(k2, "a1") == 0); /* stessa app, stessa chiave */

    /* Un pacchetto di 191 byte (il massimo) non entrerebbe in PH_FIN_APP_MAX. */
    char lungo[PH_CFG_LUN_APP];
    memset(lungo, 'x', sizeof lungo - 1);
    lungo[sizeof lungo - 1] = '\0';
    lungo[10] = '.';
    CHECK(strlen(lungo) > PH_FIN_APP_MAX);
    CHECK(ph_fa_chiave_di(t, lungo, k3));
    CHECK(strcmp(k3, "a2") == 0);
    CHECK(strlen(k3) < PH_FIN_APP_MAX);
    CHECK(strcmp(ph_fa_pacchetto_di(t, k3), lungo) == 0);
    CHECK(strcmp(ph_fa_pacchetto_di(t, "a1"), "it.finto.note") == 0);

    /* Troppo lungo anche per il profilo: rifiutato, out vuoto. */
    char enorme[PH_CFG_LUN_APP + 8];
    memset(enorme, 'y', sizeof enorme - 1);
    enorme[sizeof enorme - 1] = '\0';
    CHECK(!ph_fa_chiave_di(t, enorme, k4));
    CHECK(k4[0] == '\0');
    CHECK(!ph_fa_chiave_di(t, NULL, k4));

    /* Lo specchio ("" <-> "phone"). */
    CHECK(ph_fa_chiave_di(t, "", k4));
    CHECK(strcmp(k4, "phone") == 0);
    CHECK(strcmp(ph_fa_pacchetto_di(t, "phone"), "") == 0);

    /* Chiavi non nostre: nessuna risposta (il profilo e' un file). */
    CHECK(ph_fa_pacchetto_di(t, "a3") == NULL);
    CHECK(ph_fa_pacchetto_di(t, "a0") == NULL);
    CHECK(ph_fa_pacchetto_di(t, "a01") == NULL);
    CHECK(ph_fa_pacchetto_di(t, "a") == NULL);
    CHECK(ph_fa_pacchetto_di(t, "b1") == NULL);
    CHECK(ph_fa_pacchetto_di(t, "a1x") == NULL);
    CHECK(ph_fa_pacchetto_di(t, "a99999999999999999999") == NULL);
    CHECK(ph_fa_pacchetto_di(t, "") == NULL);
    CHECK(ph_fa_pacchetto_di(t, NULL) == NULL);

    /* Ricordi: profilo -> finestre.h -> profilo, anche con il pacchetto lungo. */
    PhCfgFinestra pf[4];
    memset(pf, 0, sizeof pf);
    g_strlcpy(pf[0].app, "it.finto.mappe", sizeof pf[0].app);
    pf[0].x = 10, pf[0].y = 20, pf[0].larghezza = 300, pf[0].altezza = 400;
    g_strlcpy(pf[1].app, lungo, sizeof pf[1].app);
    pf[1].x = -5, pf[1].y = 7, pf[1].larghezza = 500, pf[1].altezza = 250;
    pf[2].larghezza = 1; /* app vuota: scartata */
    g_strlcpy(pf[3].app, "it.finto.note", sizeof pf[3].app);
    pf[3].x = 1, pf[3].y = 2, pf[3].larghezza = 3, pf[3].altezza = 4;
    PhFinRicordo ri[4];
    size_t n = ph_fa_ricordi_da_profilo(t, pf, 4, ri, 4);
    CHECK(n == 3);
    CHECK(strcmp(ri[0].app, "a3") == 0 && ri[0].x == 10 && ri[0].w == 300 && ri[0].h == 400);
    CHECK(strcmp(ri[1].app, "a2") == 0 && ri[1].x == -5 && ri[1].y == 7);
    CHECK(strcmp(ri[2].app, "a1") == 0);
    /* Il limite max e' rispettato. */
    CHECK(ph_fa_ricordi_da_profilo(t, pf, 4, ri, 1) == 1);

    PhFinRicordo da_fin[4];
    memset(da_fin, 0, sizeof da_fin);
    g_strlcpy(da_fin[0].app, "a2", sizeof da_fin[0].app);
    da_fin[0].x = 11, da_fin[0].y = 12, da_fin[0].w = 13, da_fin[0].h = 14;
    g_strlcpy(da_fin[1].app, "phone", sizeof da_fin[1].app); /* specchio: non si salva */
    g_strlcpy(da_fin[2].app, "a77", sizeof da_fin[2].app);   /* sconosciuta: scartata */
    g_strlcpy(da_fin[3].app, "a1", sizeof da_fin[3].app);
    PhCfgFinestra fuori[4];
    n = ph_fa_ricordi_a_profilo(t, da_fin, 4, fuori, 4);
    CHECK(n == 2);
    CHECK(strcmp(fuori[0].app, lungo) == 0 && fuori[0].x == 11 && fuori[0].larghezza == 13 &&
          fuori[0].altezza == 14);
    CHECK(strcmp(fuori[1].app, "it.finto.note") == 0);
    ph_fa_chiavi_libera(t);
}

/* ---- Scelta della misura iniziale ---- */
static void prova_spec(void)
{
    PhSrvApp verticale = {.pacchetto = (char *)"p.v", .nome = (char *)"V", .solo_verticale = true};
    PhSrvApp normale = {.pacchetto = (char *)"p.n", .nome = (char *)"N"};
    /* Misura predefinita: w = h = 0, la sceglie ph_fin_apri dallo schermo del telefono. */
    PhFinSpec s = ph_fa_scegli_spec(&verticale, false);
    CHECK(s.w == 0 && s.h == 0 && s.solo_verticale && !s.specchio);
    s = ph_fa_scegli_spec(&normale, false);
    CHECK(s.w == 0 && s.h == 0 && !s.solo_verticale && !s.specchio);
    s = ph_fa_scegli_spec(NULL, false); /* elenco non ancora arrivato */
    CHECK(s.w == 0 && s.h == 0 && !s.solo_verticale && !s.specchio);
    s = ph_fa_scegli_spec(NULL, true);
    CHECK(s.w == 0 && s.h == 0 && s.specchio);
    s = ph_fa_scegli_spec(&verticale, true); /* lo specchio vince */
    CHECK(s.specchio && s.w == 0);
}

/* ---- Misure predefinite dallo schermo del telefono ---- */
static void prova_telefono(void)
{
    PhErrore e = {{0}};
    PhFinInfo i;
    PhFinRect c;
    /* Telefono non noto: ripiego di finestre.h (forma di telefono PH_FIN_TEL_W x PH_FIN_TEL_H). */
    PhFinDesktop *d = ph_fin_crea(2000, 1400, 1.0, &e);
    CHECK(d != NULL);
    if (d == NULL)
        return;
    PhFinSpec vert = ph_fa_scegli_spec(&(PhSrvApp){.pacchetto = (char *)"v", .solo_verticale = true}, false);
    unsigned id = ph_fin_apri(d, "a1", "V", &vert, 0, NULL, &e);
    CHECK(id != 0 && ph_fin_info(d, id, &i, &e) == 0 && i.rect.w == PH_FIN_TEL_W && i.rect.h == PH_FIN_TEL_H);
    ph_fin_libera(d);

    /* Telefono 1080x2340 a 450 dpi: in punti 1080*160/450 = 384 e 2340*160/450 = 832.
     * Area alta (host 2000x1400: area utile 2000x1352): la finestra generica e la
     * forma di telefono hanno contenuto 384x832 (multipli di 8, nessun assestamento)
     * e la finestra e' alta 832 + 50 del titolo = 882. */
    d = ph_fin_crea(2000, 1400, 1.0, &e);
    CHECK(d != NULL);
    if (d == NULL)
        return;
    CHECK(ph_fin_imposta_telefono(d, 1080, 2340, 450, 0, &e) == 0);
    PhFinSpec gen = ph_fa_scegli_spec(NULL, false);
    id = ph_fin_apri(d, "a1", "G", &gen, 0, NULL, &e);
    CHECK(id != 0 && ph_fin_contenuto(d, id, &c, &e) == 0 && c.w == 384 && c.h == 832);
    CHECK(ph_fin_info(d, id, &i, &e) == 0 && i.rect.h == 882 && !i.solo_verticale);
    unsigned iv = ph_fin_apri(d, "a2", "V", &vert, 10, NULL, &e);
    CHECK(iv != 0 && ph_fin_contenuto(d, iv, &c, &e) == 0 && c.w == 384 && c.h == 832);
    CHECK(ph_fin_info(d, iv, &i, &e) == 0 && i.solo_verticale && !i.puo_ridimensionare);
    /* Specchio: la proporzione dello schermo cosi' com'e' (1080x2340 = 384x832 punti). */
    PhFinSpec sp = ph_fa_scegli_spec(NULL, true);
    unsigned is = ph_fin_apri(d, "phone", "S", &sp, 20, NULL, &e);
    CHECK(is != 0 && ph_fin_contenuto(d, is, &c, &e) == 0 && c.w == 384 && c.h == 832);
    ph_fin_libera(d);

    /* Area bassa (host 2000x844: area utile 2000x796): non sta (882 > 796), si riduce
     * IN PROPORZIONE. Spazio per il contenuto: 796 - 2*16 (margine) - 50 (titolo) =
     * 714 di altezza; fattore 714/832 = 0,8582 -> larghezza 329,5, altezza 714; poi
     * l'assestamento ai multipli di 8 pixel fisici (scala 1) porta la larghezza a
     * 328 (329,5 -> 328) e l'altezza a 712 (714 e' piu' vicino a 712 che a 720;
     * la finestra alta 712+50 = 762 sta ancora nell'area di 796 salendo di
     * qualche pixel, y <= 34). */
    d = ph_fin_crea(2000, 844, 1.0, &e);
    CHECK(d != NULL);
    if (d == NULL)
        return;
    CHECK(ph_fin_imposta_telefono(d, 1080, 2340, 450, 0, &e) == 0);
    id = ph_fin_apri(d, "a1", "G", &gen, 0, NULL, &e);
    CHECK(id != 0 && ph_fin_contenuto(d, id, &c, &e) == 0);
    CHECK(c.w == 328 && c.h == 712);
    CHECK(ph_fin_info(d, id, &i, &e) == 0 && i.rect.y + i.rect.h <= 796); /* sta nell'area */
    /* Misure fuori limite: errore e nulla cambia. */
    CHECK(ph_fin_imposta_telefono(d, 0, 2340, 450, 0, NULL) != 0);
    CHECK(ph_fin_imposta_telefono(d, 1080, 2340, 10, 0, NULL) != 0);
    ph_fin_libera(d);
}

/* ---- Cartella degli screenshot e delle registrazioni ---- */
static void prova_screenshot(void)
{
    char *p = ph_fa_cartella_screenshot("/prove/Immagini");
    CHECK(strcmp(p, "/prove/Immagini/YASDE") == 0);
    g_free(p);
    p = ph_fa_cartella_screenshot("/prove/home");
    CHECK(strcmp(p, "/prove/home/YASDE") == 0);
    g_free(p);
}

/* ---- Pausa e ripresa: la simulazione di una finestra con host e minimizza ---- */
typedef struct {
    bool in_pausa;
    int pause, riprese;
} SimFlusso;

static void sim_passo(SimFlusso *s, bool host_visibile, bool minimizzata)
{
    switch (ph_fa_passo_pausa(s->in_pausa, ph_fa_pausa_voluta(host_visibile, minimizzata))) {
    case PH_FA_PASSO_PAUSA:
        s->in_pausa = true;
        s->pause++;
        break;
    case PH_FA_PASSO_RIPRENDI:
        s->in_pausa = false;
        s->riprese++;
        break;
    case PH_FA_PASSO_NIENTE:
        break;
    }
}

static void prova_pausa(void)
{
    CHECK(!ph_fa_pausa_voluta(true, false));
    CHECK(ph_fa_pausa_voluta(true, true));
    CHECK(ph_fa_pausa_voluta(false, false));
    CHECK(ph_fa_pausa_voluta(false, true));

    /* Due finestre: A normale, B minimizzata. */
    SimFlusso a = {0}, b = {0};
    sim_passo(&a, true, false);
    sim_passo(&b, true, true);
    CHECK(!a.in_pausa && b.in_pausa && a.pause == 0 && b.pause == 1);
    /* Chiamarlo di nuovo non rimanda nulla. */
    sim_passo(&b, true, true);
    CHECK(b.pause == 1 && b.riprese == 0);
    /* Host non visibile: A va in pausa, B resta (una sola pausa). */
    sim_passo(&a, false, false);
    sim_passo(&b, false, true);
    CHECK(a.in_pausa && b.in_pausa && a.pause == 1 && b.pause == 1);
    /* Host di nuovo visibile: riprende SOLO A, B e' ancora minimizzata. */
    sim_passo(&a, true, false);
    sim_passo(&b, true, true);
    CHECK(!a.in_pausa && b.in_pausa && a.riprese == 1 && b.riprese == 0);
    /* B ripristinata: riprende. */
    sim_passo(&b, true, false);
    CHECK(!b.in_pausa && b.riprese == 1);
    /* Host nascosto mentre B e' attiva e poi B minimizzata: nessuna ripresa. */
    sim_passo(&b, false, false);
    sim_passo(&b, false, true);
    sim_passo(&b, true, true);
    CHECK(b.in_pausa && b.pause == 2 && b.riprese == 1);
}

/* ---- Geometria del titolo: deve coincidere con ph_fin_trova ---- */
static void prova_titolo(void)
{
    static const PhFinPulsante tutti[] = {PH_FIN_PULS_INDIETRO,   PH_FIN_PULS_SCREENSHOT,
                                          PH_FIN_PULS_REGISTRA,   PH_FIN_PULS_ALTRI,
                                          PH_FIN_PULS_MINIMIZZA,  PH_FIN_PULS_INGRANDISCI,
                                          PH_FIN_PULS_CHIUDI};
    /* Larghezze con aree a scala 1 e a scala 1,5 (finestra 288, il minimo di finestre.c, e 380 di larghezza). */
    static const int larghezze[] = {288, 296, 380, 640};
    for (size_t li = 0; li < sizeof larghezze / sizeof *larghezze; li++) {
        PhErrore e = {{0}};
        PhFinDesktop *d = ph_fin_crea(1920, 1080, 1.5, &e);
        CHECK(d != NULL);
        if (d == NULL)
            continue;
        PhFinSpec sp = {larghezze[li], PH_FIN_TITOLO_H + 392, false, false};
        unsigned id = ph_fin_apri(d, "a1", "T", &sp, 0, NULL, &e);
        CHECK(id != 0);
        PhFinInfo info;
        CHECK(ph_fin_info(d, id, &info, &e) == 0);
        /* In regola automatica la finestra normale si assesta da sola sul multiplo di 8
         * pixel fisici piu' vicino: la larghezza puo' spostarsi di pochi pixel. */
        CHECK(info.rect.w >= larghezze[li] - 8 && info.rect.w <= larghezze[li] + 8);
        for (size_t k = 0; k < sizeof tutti / sizeof *tutti; k++) {
            int x = ph_fa_pulsante_x(info.rect.w, tutti[k]);
            PhFinColpo c;
            /* Centro del pulsante: (x + 15, y + 10 + 15). */
            ph_fin_trova(d, info.rect.x + x + PH_FIN_PULS / 2.0, info.rect.y + PUL_Y + PH_FIN_PULS / 2.0, &c);
            CHECK(c.id == id && c.zona == PH_FIN_ZONA_PULSANTE && c.pulsante == tutti[k]);
            /* Primo e ultimo punto del pulsante, e il punto subito fuori. */
            ph_fin_trova(d, info.rect.x + x + 0.5, info.rect.y + PUL_Y + 0.5, &c);
            CHECK(c.zona == PH_FIN_ZONA_PULSANTE && c.pulsante == tutti[k]);
            ph_fin_trova(d, info.rect.x + x + PH_FIN_PULS - 0.5, info.rect.y + PUL_Y + PH_FIN_PULS - 0.5, &c);
            CHECK(c.zona == PH_FIN_ZONA_PULSANTE && c.pulsante == tutti[k]);
            ph_fin_trova(d, info.rect.x + x - 0.5, info.rect.y + PH_FIN_TITOLO_H / 2.0, &c);
            CHECK(c.pulsante != tutti[k]);
        }
        ph_fin_libera(d);
    }
}

/* ---- Origine dell'immagine al pixel fisico intero ---- */
static void prova_origine(void)
{
    static const double scale[] = {1.0, 1.25, 1.5, 2.0};
    static const double origini[] = {0.0, 70.0, 62.0, 640.0, 333.0, 70.4};
    for (size_t a = 0; a < sizeof scale / sizeof *scale; a++) {
        for (size_t b = 0; b < sizeof origini / sizeof *origini; b++) {
            double o = ph_fa_origine_pixel_intero(origini[b], 38.0, scale[a]);
            double fisico = (38.0 + o) * scale[a];
            CHECK(fabs(fisico - round(fisico)) < 1e-9);          /* pixel fisico intero */
            CHECK(fabs(o - origini[b]) <= 0.5 / scale[a] + 1e-9); /* meno di mezzo pixel fisico */
        }
    }
    /* A mano: scala 1,5, scostamento 38, origine 70: (38+70)*1,5 = 162 gia' intero. */
    CHECK(fabs(ph_fa_origine_pixel_intero(70.0, 38.0, 1.5) - 70.0) < 1e-12);
    /* Origine 71: (38+71)*1,5 = 163,5 -> 164 -> 164/1,5 = 109,333 -> 71,333. */
    CHECK(fabs(ph_fa_origine_pixel_intero(71.0, 38.0, 1.5) - (164.0 / 1.5 - 38.0)) < 1e-9);
    /* Scala 1,25, origine 70, scostamento 38: 108*1,25 = 135 gia' intero; origine 71: 136,25 -> 136. */
    CHECK(fabs(ph_fa_origine_pixel_intero(70.0, 38.0, 1.25) - 70.0) < 1e-12);
    CHECK(fabs(ph_fa_origine_pixel_intero(71.0, 38.0, 1.25) - (136.0 / 1.25 - 38.0)) < 1e-9);
    /* Senza scostamento (radice a (0,0)) e scala non valida. */
    CHECK(fabs(ph_fa_origine_pixel_intero(10.0, 0.0, 1.0) - 10.0) < 1e-12);
    CHECK(ph_fa_origine_pixel_intero(10.0, 38.0, 0.0) == 10.0);
}

/* ---- Riga del registro ---- */
static void prova_riga(void)
{
    char r[512];
    ph_fa_riga_misura(r, sizeof r, "apertura", 264, 568, 1.0, 264, 568, 103, 420, false, false, 0.64, 414, 891);
    CHECK(strstr(r, "apertura: contenuto 264x568 logici = 264x568 fisici, scala 1,") != NULL ||
          strstr(r, "apertura: contenuto 264x568 logici = 264x568 fisici, scala 1") != NULL);
    CHECK(strstr(r, "chiesto al telefono 264x568 a 103 dpi") != NULL);
    CHECK(strstr(r, "fattore 0.64") != NULL);
    CHECK(strstr(r, "il telefono vede 414x891 punti") != NULL);
    CHECK(strstr(r, "modalita' tablet no") != NULL);
    /* Specchio: nessun segnaposto (ne' dpi ne' misura chiesta). */
    ph_fa_riga_misura(r, sizeof r, "apertura", 280, 500, 1.0, 288, 512, 0, 0, false, true, 1.0, 0, 0);
    CHECK(strstr(r, "misura decisa dal telefono") != NULL);
    CHECK(strstr(r, "chiesto al telefono") == NULL && strstr(r, "0 dpi") == NULL &&
          strstr(r, "288") == NULL && strstr(r, "punti") == NULL);
    /* Testo troncato ma sempre terminato. */
    char piccolo[16];
    ph_fa_riga_misura(piccolo, sizeof piccolo, "ridimensionamento", 1, 1, 1.0, 8, 8, 160, 420, true, false, 1.0, 1, 1);
    CHECK(strlen(piccolo) == sizeof piccolo - 1);
}

/* ---- Aperture contate ---- */
static uint32_t volte_di(const PhCfgTelefono *t, const char *app)
{
    for (size_t i = 0; i < t->n_aperture; i++)
        if (strcmp(t->aperture[i].app, app) == 0)
            return t->aperture[i].volte;
    return 0;
}

static void prova_aperture(void)
{
    PhCfgTelefono *t = ph_cfg_telefono_nuovo();
    CHECK(t != NULL);
    if (t == NULL)
        return;
    /* Finestra nuova: conta, con l'ora. */
    CHECK(ph_fa_conta_apertura(t, "it.finto.note", true, false, 1700000000));
    CHECK(volte_di(t, "it.finto.note") == 1 && t->aperture[0].ultima == 1700000000);
    /* Una seconda finestra nuova (dopo che la prima e' stata chiusa): +1. */
    CHECK(ph_fa_conta_apertura(t, "it.finto.note", true, false, 1700000100));
    CHECK(volte_di(t, "it.finto.note") == 2);
    /* Secondo clic che porta davanti una finestra gia' aperta (nuova = false), ripristino da
     * minimizzata e ripresa dopo una caduta del collegamento (stessa finestra): niente. */
    CHECK(!ph_fa_conta_apertura(t, "it.finto.note", false, false, 1700000200));
    CHECK(volte_di(t, "it.finto.note") == 2);
    /* Lo specchio non conta, e non crea nemmeno la voce. */
    CHECK(!ph_fa_conta_apertura(t, "", true, true, 1700000300));
    CHECK(!ph_fa_conta_apertura(t, "phone", true, true, 1700000300));
    CHECK(volte_di(t, "phone") == 0 && t->n_aperture == 1);
    /* Nome non valido o profilo assente: nulla. */
    CHECK(!ph_fa_conta_apertura(t, NULL, true, false, 1));
    CHECK(!ph_fa_conta_apertura(t, "", true, false, 1));
    CHECK(!ph_fa_conta_apertura(NULL, "it.finto.note", true, false, 1));
    CHECK(t->n_aperture == 1 && volte_di(t, "it.finto.note") == 2);
    /* Un'altra app ha il suo conto. */
    CHECK(ph_fa_conta_apertura(t, "it.finto.foto", true, false, 1700000400));
    CHECK(volte_di(t, "it.finto.foto") == 1 && t->n_aperture == 2);
    ph_cfg_telefono_libera(t);
}

/* ---- Specchio a forma fissa; finestra a cavallo del bordo inferiore ---- */
static void prova_specchio_e_bordo(void)
{
    PhErrore e = {{0}};
    PhFinInfo i;
    PhFinDesktop *d = ph_fin_crea(1920, 1080, 1.0, &e);
    CHECK(d != NULL);
    if (d == NULL)
        return;
    CHECK(ph_fin_imposta_qualita(d, 450, 160, 2560, 0, &e) == 0);
    /* Specchio: i comandi di misura sono falsi, come per le solo verticali; la cornice
     * li legge dai campi puo_* (nessun ramo a parte per lo specchio). */
    PhFinSpec sp = ph_fa_scegli_spec(NULL, true);
    unsigned is = ph_fin_apri(d, "phone", "S", &sp, 0, NULL, &e);
    CHECK(is != 0 && ph_fin_info(d, is, &i, &e) == 0 && i.specchio);
    CHECK(!i.puo_ridimensionare && !i.puo_ingrandire && !i.puo_agganciare);
    PhFinRect prima = i.rect;
    CHECK(ph_fin_massimizza_ripristina(d, is, 10, NULL) != 0);
    CHECK(ph_fin_gesto_inizia(d, is, PH_FIN_GESTO_RIDIMENSIONA, i.rect.x + i.rect.w - 5.0,
                              i.rect.y + i.rect.h - 5.0, 20, NULL) != 0);
    CHECK(ph_fin_ruota(d, is, 30, NULL) != 0);
    CHECK(ph_fin_info(d, is, &i, &e) == 0 && i.rect.w == prima.w && i.rect.h == prima.h);
    /* Contro il bordo sinistro: nessun aggancio. */
    CHECK(ph_fin_gesto_inizia(d, is, PH_FIN_GESTO_SPOSTA, i.rect.x + 50.0, i.rect.y + 10.0, 40, NULL) == 0);
    CHECK(ph_fin_gesto_muovi(d, 5, 200, 50, NULL) == 0);
    CHECK(ph_fin_gesto_anteprima(d, NULL) == PH_FIN_AGG_NESSUNO);
    CHECK(ph_fin_gesto_finisci(d, 5, 200, 60, NULL, NULL) == 0);
    CHECK(ph_fin_info(d, is, &i, &e) == 0 && i.stato == PH_FIN_NORMALE && i.rect.w == prima.w);
    CHECK(ph_fin_chiudi(d, is, 70, &e) == 0);

    /* Finestra normale trascinata a cavallo del bordo inferiore (area utile 1920x1032). */
    PhFinRect area = ph_fin_area(d);
    PhFinSpec gen = ph_fa_scegli_spec(NULL, false);
    unsigned id = ph_fin_apri(d, "a1", "G", &gen, 100, NULL, &e);
    CHECK(id != 0 && ph_fin_info(d, id, &i, &e) == 0);
    int w0 = i.rect.w, h0 = i.rect.h;
    int dens0 = i.densita, fw0 = i.flusso_w, fh0 = i.flusso_h;
    CHECK(ph_fin_gesto_inizia(d, id, PH_FIN_GESTO_SPOSTA, i.rect.x + 50.0, i.rect.y + 10.0, 110, &e) == 0);
    /* Il puntatore scende oltre il bordo inferiore e ancora oltre (sopra la barra e sotto). */
    CHECK(ph_fin_gesto_muovi(d, i.rect.x + 50.0, area.h - 20.0, 120, &e) == 0);
    CHECK(ph_fin_gesto_muovi(d, i.rect.x + 50.0, area.h + 500.0, 130, &e) == 0);
    CHECK(ph_fin_gesto_finisci(d, i.rect.x + 50.0, area.h + 500.0, 140, NULL, &e) == 0);
    CHECK(ph_fin_info(d, id, &i, &e) == 0);
    /* Sporge in basso (il titolo resta nell'area), misura e densita' invariate. */
    CHECK(i.rect.y + i.rect.h > area.h && i.rect.y < area.h);
    CHECK(i.rect.w == w0 && i.rect.h == h0 && i.densita == dens0 && i.flusso_w == fw0 &&
          i.flusso_h == fh0);
    PhFinColpo col;
    /* Titolo visibile: e' della finestra; punto nella parte che sporge: FUORI, non input. */
    ph_fin_trova(d, i.rect.x + 100.0, i.rect.y + 10.0, &col);
    CHECK(col.id == id && col.zona == PH_FIN_ZONA_TITOLO);
    ph_fin_trova(d, i.rect.x + 100.0, area.h + 5.0, &col);
    CHECK(col.id == 0 && col.zona == PH_FIN_ZONA_FUORI && !col.valido);
    ph_fin_trova(d, i.rect.x + 100.0, area.h - 0.5, &col);
    CHECK(col.id == id); /* l'ultima riga dell'area e' ancora della finestra */
    /* Nessuna richiesta al telefono per lo spostamento. */
    PhFinRichiesta rich[PH_FIN_MAX];
    CHECK(ph_fin_avanza(d, 1000, rich, PH_FIN_MAX) == 0);
    ph_fin_libera(d);
}

/* ---- Pizzico: macchina a stati ---- */
static void prova_pizzico(void)
{
    PhFaPizzico p;
    int passo = 0;
    ph_fa_pz_inizializza(&p);
    /* Nessun gesto aperto: chiudere non fa nulla, e non c'e' scadenza. */
    CHECK(ph_fa_pz_chiudi(&p) == PH_FA_PZ_NIENTE);
    CHECK(!ph_fa_pz_scaduto(&p, 100000));
    /* Primo scatto (rotella in su = +1): INIZIO con 120. */
    CHECK(ph_fa_pz_passo(&p, 1.0, 1000, &passo) == PH_FA_PZ_INIZIO && passo == 120);
    CHECK(p.aperto);
    /* Successivi: CONTINUA, anche con il segno opposto. */
    CHECK(ph_fa_pz_passo(&p, 1.0, 1100, &passo) == PH_FA_PZ_CONTINUA && passo == 120);
    CHECK(ph_fa_pz_passo(&p, -1.0, 1200, &passo) == PH_FA_PZ_CONTINUA && passo == -120);
    /* Scadenza: 250 ms dopo l'ULTIMO passo (a 1449 no, a 1450 si'). */
    CHECK(!ph_fa_pz_scaduto(&p, 1449));
    CHECK(ph_fa_pz_scaduto(&p, 1450));
    /* Un passo nuovo sposta la scadenza. */
    CHECK(ph_fa_pz_passo(&p, 1.0, 1400, &passo) == PH_FA_PZ_CONTINUA);
    CHECK(!ph_fa_pz_scaduto(&p, 1450) && ph_fa_pz_scaduto(&p, 1650));
    /* Chiusura: FINE una volta sola; poi niente e niente scadenza. */
    CHECK(ph_fa_pz_chiudi(&p) == PH_FA_PZ_FINE && !p.aperto);
    CHECK(ph_fa_pz_chiudi(&p) == PH_FA_PZ_NIENTE);
    CHECK(!ph_fa_pz_scaduto(&p, 1000000));
    /* Dopo la FINE il passo successivo e' di nuovo un INIZIO. */
    CHECK(ph_fa_pz_passo(&p, 1.0, 5000, &passo) == PH_FA_PZ_INIZIO);
    ph_fa_pz_chiudi(&p);

    /* Touchpad: frazioni. 0,004 scatti = 0,48 passi: niente, e il gesto NON si apre. */
    ph_fa_pz_inizializza(&p);
    CHECK(ph_fa_pz_passo(&p, 0.004, 10, &passo) == PH_FA_PZ_NIENTE && !p.aperto);
    /* Il resto si somma: 0,004 + 0,005 = 1,08 passi -> 1 passo, INIZIO. */
    CHECK(ph_fa_pz_passo(&p, 0.005, 20, &passo) == PH_FA_PZ_INIZIO && passo == 1);
    /* Una frazione che non fa un passo non sposta la scadenza. */
    CHECK(ph_fa_pz_passo(&p, 0.001, 200, &passo) == PH_FA_PZ_NIENTE);
    CHECK(ph_fa_pz_scaduto(&p, 270) && !ph_fa_pz_scaduto(&p, 269));
    ph_fa_pz_chiudi(&p);
    /* Limite del contratto: -1200..1200. */
    ph_fa_pz_inizializza(&p);
    CHECK(ph_fa_pz_passo(&p, 50.0, 1, &passo) == PH_FA_PZ_INIZIO && passo == 1200);
    CHECK(ph_fa_pz_passo(&p, -50.0, 2, &passo) == PH_FA_PZ_CONTINUA && passo == -1200);
    CHECK(ph_fa_pz_passo(&p, NAN, 3, &passo) == PH_FA_PZ_NIENTE);
    /* Dopo una chiusura il resto si azzera. */
    ph_fa_pz_chiudi(&p);
    CHECK(ph_fa_pz_passo(&p, 0.004, 4, &passo) == PH_FA_PZ_NIENTE);
    CHECK(ph_fa_pz_chiudi(&p) == PH_FA_PZ_NIENTE);

    /* Ctrl senza Alt = zoom (e quindi nessuna rotella normale). */
    CHECK(ph_fa_scroll_e_zoom(GDK_CONTROL_MASK));
    CHECK(ph_fa_scroll_e_zoom(GDK_CONTROL_MASK | GDK_SHIFT_MASK));
    CHECK(!ph_fa_scroll_e_zoom((GdkModifierType)0));
    CHECK(!ph_fa_scroll_e_zoom(GDK_SHIFT_MASK));
    CHECK(!ph_fa_scroll_e_zoom(GDK_CONTROL_MASK | GDK_ALT_MASK));

    /* Touchpad (GtkGestureZoom): x1,25 = 1 scatto = 120 passi; inverso = -120; x1,25^2 = 2. */
    CHECK(fabs(ph_fa_scatti_da_rapporto(1.25) - 1.0) < 1e-9);
    CHECK(fabs(ph_fa_scatti_da_rapporto(1.0 / 1.25) + 1.0) < 1e-9);
    CHECK(fabs(ph_fa_scatti_da_rapporto(1.5625) - 2.0) < 1e-9);
    CHECK(ph_fa_scatti_da_rapporto(1.0) == 0.0 && ph_fa_scatti_da_rapporto(0.0) == 0.0 &&
          ph_fa_scatti_da_rapporto(-1.0) == 0.0 && ph_fa_scatti_da_rapporto(NAN) == 0.0);
    /* Rapporti piccoli ripetuti si accumulano senza perdere il resto: dieci x1,02 =
     * 10 * log(1,02)/log(1,25) = 0,8874 scatti = 106,5 passi: la somma dei passi mandati
     * piu' il resto fa 106,5. */
    ph_fa_pz_inizializza(&p);
    int somma = 0;
    for (int k = 0; k < 10; k++)
        if (ph_fa_pz_passo(&p, ph_fa_scatti_da_rapporto(1.02), (uint64_t)k * 10, &passo) != PH_FA_PZ_NIENTE)
            somma += passo;
    CHECK(somma == 106 && fabs(p.resto - 0.5) < 0.05);
}

/* ---- Pizzico a due dita su touchscreen: che fare del tocco del primo dito ---- */
static void prova_zoom_touch(void)
{
    CHECK(ph_fa_zoom_inizio(false, false) == PH_FA_ZOOM_PROCEDI);
    CHECK(ph_fa_zoom_inizio(true, false) == PH_FA_ZOOM_ANNULLA_TOCCO);
    CHECK(ph_fa_zoom_inizio(false, true) == PH_FA_ZOOM_RIFIUTA);
    /* Lo spostamento di una finestra vince su un eventuale tocco. */
    CHECK(ph_fa_zoom_inizio(true, true) == PH_FA_ZOOM_RIFIUTA);
}

/* ---- Compatibilita' (CONTRATTI.md §19): profilo e finestre.h ---- */
static void prova_compatibilita(void)
{
    PhErrore e = {{0}};
    PhCfgTelefono *t = ph_cfg_telefono_nuovo();
    CHECK(t != NULL);
    if (t == NULL)
        return;
    /* Profilo: la voce parte spenta; si attiva, resta, si toglie. */
    CHECK(!ph_cfg_telefono_in_compatibilita(t, "it.finto.vecchia"));
    CHECK(ph_cfg_telefono_imposta_compatibilita(t, "it.finto.vecchia", true, &e) == 0);
    CHECK(ph_cfg_telefono_in_compatibilita(t, "it.finto.vecchia") && t->n_compatibilita == 1);
    CHECK(ph_cfg_telefono_imposta_compatibilita(t, "it.finto.vecchia", true, &e) == 0 && t->n_compatibilita == 1);
    CHECK(ph_cfg_telefono_imposta_compatibilita(t, "it.finto.vecchia", false, &e) == 0);
    CHECK(!ph_cfg_telefono_in_compatibilita(t, "it.finto.vecchia") && t->n_compatibilita == 0);
    /* Elenco pieno: errore con la frase, nulla cambia (la spunta deve tornare com'era). */
    char nome[64];
    for (int k = 0; k < PH_CFG_MAX_COMPAT; k++) {
        snprintf(nome, sizeof nome, "it.finto.app%d", k);
        CHECK(ph_cfg_telefono_imposta_compatibilita(t, nome, true, &e) == 0);
    }
    PhErrore pieno = {{0}};
    CHECK(ph_cfg_telefono_imposta_compatibilita(t, "it.finto.una.in.piu", true, &pieno) != 0);
    CHECK(pieno.testo[0] != '\0' && t->n_compatibilita == PH_CFG_MAX_COMPAT);
    CHECK(!ph_cfg_telefono_in_compatibilita(t, "it.finto.una.in.piu"));
    ph_cfg_telefono_libera(t);

    /* finestre.h: telefono 1080x2340 a 450 dpi, area alta (host 2000x1400, scala 1). */
    PhFinDesktop *d = ph_fin_crea(2000, 1400, 1.0, &e);
    CHECK(d != NULL);
    if (d == NULL)
        return;
    CHECK(ph_fin_imposta_telefono(d, 1080, 2340, 450, 0, &e) == 0);
    CHECK(ph_fin_imposta_qualita(d, 450, 160, 2560, 0, &e) == 0);
    PhFinSpec gen = ph_fa_scegli_spec(NULL, false);
    unsigned id = ph_fin_apri(d, "a1", "G", &gen, 0, NULL, &e);
    int w = 0, h = 0, dens = 0;
    PhFinInfo i;
    /* Senza compatibilita': densita' 160 (160*scala), flusso 1:1 = contenuto 384x832. */
    CHECK(ph_fin_misura_flusso_densita(d, id, &w, &h, &dens, &e) == 0 && w == 384 && h == 832 && dens == 160);
    CHECK(!ph_fin_compatibilita(d, id));
    /* Si attiva PRIMA della prima richiesta di flusso (come all'apertura): densita' del
     * telefono, flusso = punti * 450/160 = 384*2,8125 = 1080 e 832*2,8125 = 2340 (che
     * e' meta' tra 2336 e 2344: vince il maggiore). */
    PhFinRichiesta rich[PH_FIN_MAX];
    CHECK(ph_fin_imposta_compatibilita(d, id, true, 10, &e) == 0 && ph_fin_compatibilita(d, id));
    CHECK(ph_fin_misura_flusso_densita(d, id, &w, &h, &dens, &e) == 0);
    CHECK(w == 1080 && h == 2344 && dens == 450);
    CHECK(ph_fin_info(d, id, &i, &e) == 0 && i.utile_w == 0 && i.utile_h == 0); /* niente 1:1 */
    /* La richiesta esce SUBITO (senza i 300 ms) con misura E densita'. */
    CHECK(ph_fin_avanza(d, 11, rich, PH_FIN_MAX) == 1 && rich[0].w == 1080 && rich[0].h == 2344 &&
          rich[0].densita == 450);
    CHECK(ph_fin_avanza(d, 12, rich, PH_FIN_MAX) == 0); /* una volta sola */
    /* Stesso valore: nessuna richiesta. */
    CHECK(ph_fin_imposta_compatibilita(d, id, true, 20, &e) == 0);
    CHECK(ph_fin_avanza(d, 21, rich, PH_FIN_MAX) == 0);
    /* Si spegne: torna la regola automatica, con la richiesta immediata. */
    CHECK(ph_fin_imposta_compatibilita(d, id, false, 30, &e) == 0 && !ph_fin_compatibilita(d, id));
    CHECK(ph_fin_avanza(d, 31, rich, PH_FIN_MAX) == 1 && rich[0].w == 384 && rich[0].h == 832 &&
          rich[0].densita == 160);
    /* Durante una registrazione e' rifiutata (la voce e' disattivata) e nulla cambia. */
    CHECK(ph_fin_imposta_registrazione(d, id, true, 40, &e) == 0);
    CHECK(ph_fin_imposta_compatibilita(d, id, true, 41, NULL) != 0 && !ph_fin_compatibilita(d, id));
    CHECK(ph_fin_imposta_registrazione(d, id, false, 42, &e) == 0);
    /* Specchio: la compatibilita' non esiste. */
    PhFinSpec sp = ph_fa_scegli_spec(NULL, true);
    unsigned is = ph_fin_apri(d, "phone", "S", &sp, 50, NULL, &e);
    CHECK(is != 0 && ph_fin_imposta_compatibilita(d, is, true, 51, NULL) != 0 && !ph_fin_compatibilita(d, is));
    /* Id sconosciuto. */
    CHECK(ph_fin_imposta_compatibilita(d, 9999, true, 60, NULL) != 0);
    ph_fin_libera(d);
}

/* ---- Rotella (120esimi di scatto) ---- */
static void prova_rotella(void)
{
    double acc = 0.0;
    CHECK(ph_fa_passi_rotella(&acc, 1.0) == 120);
    CHECK(ph_fa_passi_rotella(&acc, -1.0) == -120);
    /* Touchpad: frazioni che si accumulano e non si perdono. */
    int tot = 0;
    for (int i = 0; i < 100; i++)
        tot += ph_fa_passi_rotella(&acc, 0.01);
    CHECK(fabs(tot + acc - 120.0) < 1e-6 && tot >= 118 && tot <= 120);
    acc = 0.0;
    CHECK(ph_fa_passi_rotella(&acc, 0.004) == 0); /* meno di 1/120: resta nel resto */
    CHECK(ph_fa_passi_rotella(&acc, 0.005) == 1);
    CHECK(ph_fa_passi_rotella(&acc, NAN) == 0);
    /* Touchpad: 20 pixel NON sono 20 scatti (a 15 px per scatto sono 1,33). */
    CHECK(fabs(ph_fa_scatti_da_delta(true, 15.0, 30.0) - 2.0) < 1e-9);
    CHECK(fabs(ph_fa_scatti_da_delta(false, 15.0, 1.0) - 1.0) < 1e-9);
    CHECK(ph_fa_scatti_da_delta(true, 0.0, 30.0) == 0.0);
    acc = 0.0;
    CHECK(ph_fa_passi_rotella(&acc, ph_fa_scatti_da_delta(true, 15.0, 20.0)) == 160);
    CHECK(ph_fa_passi_rotella(&acc, ph_fa_scatti_da_delta(true, 15.0, 1.0)) == 8); /* resto .0 + 8 */
    acc = 0.0;
    CHECK(ph_fa_passi_rotella(&acc, 1e12) == 7680); /* tetto: 64 scatti */
}

/* ---- Input nel punto giusto: numeri veri, scala frazionaria ---- */
static void prova_input(void)
{
    PhErrore e = {{0}};
    /* Host 1920x1080 fisici a scala 1,5: area 1280x720 logici, utile 1280x672. */
    PhFinDesktop *d = ph_fin_crea(1920, 1080, 1.5, &e);
    CHECK(d != NULL);
    if (d == NULL)
        return;
    PhFinRect area = ph_fin_area(d);
    CHECK(area.w == 1280 && area.h == 672);
    /* Come finestra-app: densita' del telefono 420 (qui senza profilo). */
    CHECK(ph_fin_imposta_qualita(d, 420, 160, 2560, 0, &e) == 0);

    PhFinSpec sp = {384, PH_FIN_TITOLO_H + 392, false, false};
    unsigned id = ph_fin_apri(d, "a1", "Note", &sp, 0, NULL, &e);
    CHECK(id != 0);
    PhFinInfo info;
    PhFinRect c;
    CHECK(ph_fin_info(d, id, &info, &e) == 0);
    CHECK(ph_fin_contenuto(d, id, &c, &e) == 0);
    /* La posizione e la misura esatte dopo l'assestamento le decide finestre.h (e
     * ne cambia la regola): qui si legge il rettangolo e da quello si calcola a
     * mano il resto. Il contenuto e' sotto il titolo di PH_FIN_TITOLO_H. */
    CHECK(c.x == info.rect.x && c.y == info.rect.y + PH_FIN_TITOLO_H && c.w == info.rect.w);
    /* A mano (regola dell'assestamento: multiplo di 8 fisico piu' vicino, a pari
     * distanza il maggiore): larghezza 384*1,5 = 576 = 72*8 -> 384; altezza del
     * contenuto 392*1,5 = 588, a meta' tra 584 e 592 -> 592 -> 592/1,5 = 394,67 -> 395. */
    CHECK(c.w == 384 && c.h == 395);

    /* Regola automatica unica (come DeX): flusso = pixel fisici del contenuto utile
     * = 8 * floor((logici*1,5 + 0,5) / 8), densita' = 160 * scala a scalini di 20 =
     * 240 (multiplo di 20, sotto i 420 del telefono). */
    const int uw_atteso = 8 * (int)floor((c.w * 1.5 + 0.5) / 8.0);
    const int uh_atteso = 8 * (int)floor((c.h * 1.5 + 0.5) / 8.0);
    int fw = 0, fh = 0, dn = 0;
    CHECK(ph_fin_misura_flusso_densita(d, id, &fw, &fh, &dn, &e) == 0);
    CHECK(fw == uw_atteso && fh == uh_atteso && dn == 240);
    CHECK(info.utile_w == uw_atteso && info.utile_h == uh_atteso);
    /* L'immagine disegnata (superficie 1:1) e' utile/scala. */
    double iw0 = info.utile_w / 1.5, ih0 = info.utile_h / 1.5;
    /* Il video dichiara una misura QUALUNQUE (decide il telefono): per i numeri
     * dell'input se ne usa una fissa, indipendente dalla regola. */
    fw = 1000;
    fh = 1016;

    /* Prima che il video dichiari una misura: nessun input. */
    PhFinColpo col;
    ph_fin_trova(d, c.x + 100, c.y + 100, &col);
    CHECK(col.id == id && col.zona == PH_FIN_ZONA_CONTENUTO && !col.valido);

    CHECK(ph_fin_dichiara_video(d, id, fw, fh, &e) == 0);
    /* x * video / larghezza dell'immagine, troncato (mai arrotondato). */
    ph_fin_trova(d, c.x + 100, c.y + 100, &col);
    CHECK(col.valido && col.zona == PH_FIN_ZONA_CONTENUTO);
    CHECK(col.video_w == 1000 && col.video_h == 1016);
    CHECK((int)col.vx == (int)(100.0 * 1000 / iw0) && (int)col.vy == (int)(100.0 * 1016 / ih0));
    /* Meta' dell'immagine in orizzontale: 500 esatto. */
    ph_fin_trova(d, c.x + iw0 / 2.0, c.y + 100, &col);
    CHECK((int)col.vx == 500 || (int)col.vx == 499); /* meta' puo' cadere appena sotto per l'arrotondamento */
    /* Puntatore frazionario (mouse con scala 1,5): si tronca. */
    ph_fin_trova(d, c.x + 0.4, c.y + 0.9, &col);
    CHECK((int)col.vx == (int)(0.4 * 1000 / iw0) && (int)col.vy == (int)(0.9 * 1016 / ih0));
    /* Ultima colonna e ultima riga dell'immagine (fuori dalla maniglia 18x18 in
     * basso a destra): sempre dentro il video. */
    double ultima_x = MIN(iw0, (double)c.w) - 0.01, ultima_y = MIN(ih0, (double)c.h) - 0.01;
    ph_fin_trova(d, c.x + ultima_x, c.y + 100, &col);
    CHECK(col.zona == PH_FIN_ZONA_CONTENUTO && col.valido && (int)col.vx == (int)(ultima_x * 1000 / iw0));
    CHECK((int)col.vx == 999);
    ph_fin_trova(d, c.x + 100, c.y + ultima_y, &col);
    CHECK(col.zona == PH_FIN_ZONA_CONTENUTO && col.valido && (int)col.vy == (int)(ultima_y * 1016 / ih0));
    CHECK((int)col.vy >= 1014 && (int)col.vy <= 1015);
    /* L'angolo e' la maniglia di ridimensionamento, non input. */
    ph_fin_trova(d, c.x + c.w - 0.01, c.y + c.h - 0.01, &col);
    CHECK(col.zona == PH_FIN_ZONA_ANGOLO && !col.valido);
    /* Sul titolo non e' input, e il bordo del contenuto sta a y = 62. */
    ph_fin_trova(d, c.x + 30, c.y - 0.5, &col);
    CHECK(col.zona == PH_FIN_ZONA_TITOLO && !col.valido);
    /* Fuori da ogni finestra: il desktop. */
    ph_fin_trova(d, 5, 5, &col);
    CHECK(col.id == 0 && col.zona == PH_FIN_ZONA_FUORI);

    /* Con l'immagine stesa su tutto il contenuto, ph_fa_traduci_immagine coincide con
     * ph_fin_trova (finestre.h) dentro il contenuto. */
    const double punti[][2] = {{c.x + 100, c.y + 100}, {c.x + 0.4, c.y + 0.9}, {c.x + c.w - 0.01, c.y + 100.25}, {c.x + 130.5, c.y + 150.25}};
    for (size_t k = 0; k < sizeof punti / sizeof *punti; k++) {
        int vx = -1, vy = -1;
        ph_fin_trova(d, punti[k][0], punti[k][1], &col);
        CHECK(ph_fa_traduci_immagine(c.x, c.y, iw0, ih0, 1000, 1016, punti[k][0], punti[k][1],
                                     false, &vx, &vy));
        CHECK(vx == (int)col.vx && vy == (int)col.vy);
    }
    int vx = 0, vy = 0;
    /* Tocco gia' iniziato: il punto resta dentro l'immagine; non iniziato: fuori = no. */
    CHECK(ph_fa_traduci_immagine(c.x, c.y, iw0, ih0, 1000, 1016, -300, -300, true, &vx, &vy) &&
          vx == 0 && vy == 0);
    CHECK(ph_fa_traduci_immagine(c.x, c.y, iw0, ih0, 1000, 1016, 9999, 9999, true, &vx, &vy) &&
          vx == 999 && vy == 1015);
    CHECK(!ph_fa_traduci_immagine(c.x, c.y, iw0, ih0, 1000, 1016, -300, 100, false, &vx, &vy));
    CHECK(!ph_fa_traduci_immagine(c.x, c.y, c.w, c.h, 0, 1016, 100, 100, false, &vx, &vy));
    CHECK(!ph_fa_traduci_immagine(c.x, c.y, iw0, ih0, 1000, 1016, NAN, 100, false, &vx, &vy));
    CHECK(!ph_fa_traduci_immagine(c.x, c.y, 0, ih0, 1000, 1016, 100, 100, false, &vx, &vy));

    /* BANDE (funzione pura, numeri a mano): contenuto 380x388 in (70,62); se il video
     * ha proporzioni diverse l'immagine e' 380x190 centrata in verticale: y da
     * 62+99 a 62+289, bande sopra e sotto. */
    double ix = 70, iw = 380, ih = 190, iy = 62 + (388 - 190) / 2.0;
    CHECK(!ph_fa_traduci_immagine(ix, iy, iw, ih, 1000, 500, 100, 62 + 10, false, &vx, &vy));
    CHECK(!ph_fa_traduci_immagine(ix, iy, iw, ih, 1000, 500, 100, 62 + 380, false, &vx, &vy));
    CHECK(!ph_fa_traduci_immagine(ix, iy, iw, ih, 1000, 500, 100, iy - 0.01, false, &vx, &vy));
    CHECK(!ph_fa_traduci_immagine(ix, iy, iw, ih, 1000, 500, 100, iy + ih, false, &vx, &vy));
    /* Dentro l'immagine: centro = meta' del video; primo e ultimo punto. */
    CHECK(ph_fa_traduci_immagine(ix, iy, iw, ih, 1000, 500, ix + 190, iy + 95, false, &vx, &vy) &&
          vx == 500 && vy == 250);
    CHECK(ph_fa_traduci_immagine(ix, iy, iw, ih, 1000, 500, ix, iy, false, &vx, &vy) && vx == 0 &&
          vy == 0);
    CHECK(ph_fa_traduci_immagine(ix, iy, iw, ih, 1000, 500, ix + 379.99, iy + 189.99, false, &vx,
                                 &vy) &&
          vx == 999 && vy == 499);
    /* Tocco iniziato sull'immagine e trascinato nella banda: resta sul bordo. */
    CHECK(ph_fa_traduci_immagine(ix, iy, iw, ih, 1000, 500, 100, 62 + 5, true, &vx, &vy) &&
          vy == 0);

    /* ---- Ridimensionamento: nessuna richiesta durante il gesto; una sola
     * dopo 300 ms fermi; l'input segue la misura NUOVA dichiarata. ---- */
    PhFinRichiesta rich[PH_FIN_MAX];
    uint64_t t = 1000;
    /* La misura di apertura e' quella data all'apertura: niente richieste. */
    CHECK(ph_fin_avanza(d, t, rich, PH_FIN_MAX) == 0);
    CHECK(ph_fin_avanza(d, t + 400, rich, PH_FIN_MAX) == 0);
    /* Angolo in basso a destra (18x18). */
    double px = info.rect.x + info.rect.w - 5, py = info.rect.y + info.rect.h - 5;
    ph_fin_trova(d, px, py, &col);
    CHECK(col.zona == PH_FIN_ZONA_ANGOLO);
    CHECK(ph_fin_gesto_inizia(d, id, PH_FIN_GESTO_RIDIMENSIONA, px, py, t, &e) == 0);
    for (int i = 1; i <= 20; i++) {
        CHECK(ph_fin_gesto_muovi(d, px + i * 10.5, py + i * 6.25, t + (uint64_t)i * 16, &e) == 0);
        CHECK(ph_fin_avanza(d, t + (uint64_t)i * 16, rich, PH_FIN_MAX) == 0);
    }
    /* Gesto ancora aperto anche dopo un secondo: nessuna richiesta. */
    CHECK(ph_fin_avanza(d, t + 1500, rich, PH_FIN_MAX) == 0);
    uint64_t fine = t + 2000;
    CHECK(ph_fin_gesto_finisci(d, px + 210, py + 125, fine, NULL, &e) == 0);
    /* Subito dopo e a 299 ms: ancora niente. */
    CHECK(ph_fin_avanza(d, fine, rich, PH_FIN_MAX) == 0);
    CHECK(ph_fin_avanza(d, fine + 299, rich, PH_FIN_MAX) == 0);
    /* A 300 ms: UNA richiesta, con la misura nuova. */
    CHECK(ph_fin_avanza(d, fine + 300, rich, PH_FIN_MAX) == 1);
    CHECK(rich[0].id == id);
    /* Il flusso richiesto e' il contenuto utile (multipli di 8 fisici) a 240 dpi. */
    CHECK(ph_fin_info(d, id, &info, &e) == 0);
    CHECK(rich[0].w == info.utile_w && rich[0].h == info.utile_h && rich[0].densita == 240);
    CHECK(rich[0].w == 888 && rich[0].h == 784);
    /* E non si ripete. */
    CHECK(ph_fin_avanza(d, fine + 900, rich, PH_FIN_MAX) == 0);

    /* La misura nuova dichiarata dal video: input subito dopo il ridimensionamento. */
    CHECK(ph_fin_info(d, id, &info, &e) == 0);
    CHECK(ph_fin_contenuto(d, id, &c, &e) == 0);
    /* A mano, dopo +210 e +125 sul 384x395: 594*1,5 = 891 -> 888 -> 592; altezza
     * 520*1,5 = 780, a meta' tra 776 e 784 -> 784 -> 522,67 -> 523. Utile 888x784. */
    CHECK(c.w == 592 && c.h == 523);
    CHECK(info.utile_w == 888 && info.utile_h == 784);
    const double nw_iw = info.utile_w / 1.5, nw_ih = info.utile_h / 1.5;
    int nw = 1544, nh = 1344; /* quello che il telefono dichiara (multipli di 8) */
    CHECK(ph_fin_dichiara_video(d, id, nw, nh, &e) == 0);
    ph_fin_trova(d, c.x + nw_iw / 2.0, c.y + nw_ih / 2.0, &col);
    CHECK(col.valido && col.video_w == nw && col.video_h == nh);
    CHECK((int)col.vx == nw / 2 && (int)col.vy == nh / 2);
    /* Un punto calcolato con le misure vecchie (1000x1016) non coincide: il
     * telefono lo scarta perche' porta video_w/video_h vecchi. Qui si verifica
     * che i colpi nuovi NON portano mai la misura vecchia. */
    CHECK(col.video_w != 1000 && col.video_h != 1016);
    /* Il rilascio di un tocco iniziato prima del ridimensionamento usa la
     * misura attuale: stesso punto. */
    CHECK(ph_fa_traduci_immagine(c.x, c.y, nw_iw, nw_ih, nw, nh, c.x + nw_iw / 2.0,
                                 c.y + nw_ih / 2.0, true, &vx, &vy) &&
          vx == (int)col.vx && vy == (int)col.vy);

    /* Minimizzata: nessun input e nessuna richiesta di misura. */
    CHECK(ph_fin_minimizza(d, id, fine + 2000, &e) == 0);
    ph_fin_trova(d, c.x + 10, c.y + 10, &col);
    CHECK(col.id == 0);
    ph_fin_libera(d);
}

/* ---- Densita' effettiva (regola automatica unica) e app solo verticali ---- */
static void prova_tablet(void)
{
    PhErrore e = {{0}};
    /* Host 2000x844 fisici a scala 1, telefono a 450 dpi. */
    PhFinDesktop *d = ph_fin_crea(2000, 844, 1.0, &e);
    CHECK(d != NULL);
    if (d == NULL)
        return;
    CHECK(ph_fin_imposta_qualita(d, 450, 160, 2560, 0, &e) == 0);
    PhFinSpec sp = {384, PH_FIN_TITOLO_H + 392, false, false};
    unsigned id = ph_fin_apri(d, "a1", "Browser", &sp, 0, NULL, &e);
    CHECK(id != 0);
    int w = 0, h = 0, dens = 0, dens_piccola = 0;
    PhFinInfo i;
    /* Finestra piccola, contenuto 384x392 a scala 1 (gia' multipli di 8): flusso 1:1 =
     * 384x392; densita' 160 (160*scala). */
    CHECK(ph_fin_misura_flusso_densita(d, id, &w, &h, &dens, &e) == 0);
    CHECK(w == 384 && h == 392 && dens == 160);
    dens_piccola = dens;
    CHECK(ph_fin_info(d, id, &i, &e) == 0 && i.densita == 160 && !i.tablet);

    /* Ingrandita (contenuto 2000x746, area utile 796 - titolo 50): utile 2000x744 (746/8 =
     * 93,25; +0,5 -> floor 93 = 744; resta un margine di 2 px in basso). La densita' NON cambia tra piccola e
     * grande a pari scala. */
    CHECK(ph_fin_massimizza_ripristina(d, id, 1000, &e) == 0);
    CHECK(ph_fin_misura_flusso_densita(d, id, &w, &h, &dens, &e) == 0);
    CHECK(w == 2000 && h == 744 && dens == 160 && dens == dens_piccola);
    CHECK(ph_fin_info(d, id, &i, &e) == 0 && i.densita == 160 && i.tablet); /* tablet: solo informazione */
    PhFinRichiesta rich[PH_FIN_MAX];
    /* La richiesta parte a movimento assestato, UNA volta, con misura E densita'. */
    CHECK(ph_fin_avanza(d, 1299, rich, PH_FIN_MAX) == 0);
    CHECK(ph_fin_avanza(d, 1300, rich, PH_FIN_MAX) == 1);
    CHECK(rich[0].id == id && rich[0].w == 2000 && rich[0].h == 744 && rich[0].densita == 160);
    CHECK(ph_fin_avanza(d, 2000, rich, PH_FIN_MAX) == 0); /* non due volte la stessa */
    /* Margine di 2 px in basso (utile 2000x744 su contenuto 2000x746): un punto li' e'
     * CONTENUTO ma non e' input; un punto dentro l'immagine lo e'. */
    CHECK(ph_fin_info(d, id, &i, &e) == 0 && i.utile_w == 2000 && i.utile_h == 744);
    CHECK(ph_fin_dichiara_video(d, id, 2000, 744, &e) == 0);
    PhFinColpo cm;
    ph_fin_trova(d, 1000, i.rect.y + PH_FIN_TITOLO_H + 745.0, &cm);
    CHECK(cm.id == id && cm.zona == PH_FIN_ZONA_CONTENUTO && !cm.valido);
    ph_fin_trova(d, 1000, i.rect.y + PH_FIN_TITOLO_H + 100.0, &cm);
    CHECK(cm.zona == PH_FIN_ZONA_CONTENUTO && cm.valido && (int)cm.vx == 1000 && (int)cm.vy == 100);
    /* Torna piccola: cambia la misura, la densita' resta 160. */
    CHECK(ph_fin_massimizza_ripristina(d, id, 3000, &e) == 0);
    CHECK(ph_fin_avanza(d, 3300, rich, PH_FIN_MAX) == 1);
    CHECK(rich[0].w == 384 && rich[0].h == 392 && rich[0].densita == 160);

    /* Densita' scelta dall'utente (profilo): FISSA. 2000x746 * 320/160 = 4000x1492
     * -> tetto 2560: pixel ridotti, densita' 320. */
    uint64_t t = 4000;
    CHECK(ph_fa_densita_automatica(0));
    CHECK(!ph_fa_densita_automatica(320));
    CHECK(ph_fin_imposta_qualita(d, 320, 160, 2560, t, &e) == 0);
    CHECK(ph_fin_imposta_densita_automatica(d, ph_fa_densita_automatica(320), t, &e) == 0);
    CHECK(ph_fin_massimizza_ripristina(d, id, t + 10, &e) == 0);
    CHECK(ph_fin_misura_flusso_densita(d, id, &w, &h, &dens, &e) == 0);
    CHECK(dens == 320 && w == 2560 && h <= 1000);
    /* Torna a 0: la regola automatica riprende (ingrandita = 160 dpi, 2000x744). */
    CHECK(ph_fin_imposta_qualita(d, 450, 160, 2560, t + 20, &e) == 0);
    CHECK(ph_fin_imposta_densita_automatica(d, ph_fa_densita_automatica(0), t + 20, &e) == 0);
    CHECK(ph_fin_misura_flusso_densita(d, id, &w, &h, &dens, &e) == 0 && dens == 160 && w == 2000);
    CHECK(ph_fin_massimizza_ripristina(d, id, t + 30, &e) == 0);

    /* Specchio: nessuna densita'. */
    PhFinSpec sm = {280, 500, true, true};
    unsigned ids = ph_fin_apri(d, "phone", "Schermo", &sm, t + 200, NULL, &e);
    CHECK(ids != 0);
    CHECK(ph_fin_misura_flusso_densita(d, ids, &w, &h, &dens, NULL) != 0);
    ph_fin_libera(d);

    /* Altre scale, a mano. Scala 2 (host 2000x844 fisici = 1000x422 logici):
     * area utile 1000x374: la finestra e' 384x300 (sta nell'area), contenuto 384x268
     * logici = 768x536 fisici, 768/8 = 96 e 536/8 = 67 (gia' multipli di 8, nessun
     * assestamento); densita' 160*2 = 320 (sotto 450). */
    d = ph_fin_crea(2000, 844, 2.0, &e);
    CHECK(d != NULL);
    if (d == NULL)
        return;
    CHECK(ph_fin_imposta_qualita(d, 450, 160, 2560, 0, &e) == 0);
    PhFinSpec bassa = {384, PH_FIN_TITOLO_H + 268, false, false};
    id = ph_fin_apri(d, "a1", "Browser", &bassa, 0, NULL, &e);
    CHECK(ph_fin_misura_flusso_densita(d, id, &w, &h, &dens, &e) == 0);
    CHECK(w == 768 && h == 536 && dens == 320);
    /* Mai sopra la densita' del telefono: telefono a 200 dpi, scala 2 (volevo 320). */
    CHECK(ph_fin_imposta_qualita(d, 200, 160, 2560, 100, &e) == 0);
    CHECK(ph_fin_misura_flusso_densita(d, id, &w, &h, &dens, &e) == 0 && dens == 200);
    ph_fin_libera(d);
    /* Scala 1,5 (host 1920x1080 = 1280x720 logici): 240 dpi (160*1,5). */
    d = ph_fin_crea(1920, 1080, 1.5, &e);
    CHECK(d != NULL);
    if (d == NULL)
        return;
    CHECK(ph_fin_imposta_qualita(d, 450, 160, 2560, 0, &e) == 0);
    id = ph_fin_apri(d, "a1", "Browser", &sp, 0, NULL, &e);
    CHECK(ph_fin_misura_flusso_densita(d, id, &w, &h, &dens, &e) == 0);
    /* Scala 1,5: come sopra (contenuto 384x389 logici -> utile 576x584 fisici). */
    CHECK(w % 8 == 0 && h % 8 == 0 && w >= 568 && w <= 584 && dens == 240);
    ph_fin_libera(d);
}

/* ---- App solo verticali: la finestra non cambia dimensioni ---- */
static void prova_solo_verticale(void)
{
    PhErrore e = {{0}};
    PhFinDesktop *d = ph_fin_crea(1920, 1080, 1.0, &e);
    CHECK(d != NULL);
    if (d == NULL)
        return;
    CHECK(ph_fin_imposta_qualita(d, 450, 160, 2560, 0, &e) == 0);
    PhFinInfo i;
    PhFinSpec vert = ph_fa_scegli_spec(&(PhSrvApp){.pacchetto = (char *)"p.v", .nome = (char *)"V",
                                                    .solo_verticale = true},
                                       false);
    unsigned v = ph_fin_apri(d, "a1", "V", &vert, 0, NULL, &e);
    CHECK(v != 0);
    CHECK(ph_fin_info(d, v, &i, &e) == 0 && i.solo_verticale);
    /* I tre comandi sono disattivati; i campi li lascia false finestre.h. */
    CHECK(!i.puo_ridimensionare && !i.puo_ingrandire && !i.puo_agganciare);
    /* Chiamarli comunque non cambia nulla e torna errore. */
    PhFinRect prima = i.rect;
    CHECK(ph_fin_massimizza_ripristina(d, v, 10, NULL) != 0);
    CHECK(ph_fin_gesto_inizia(d, v, PH_FIN_GESTO_RIDIMENSIONA, i.rect.x + i.rect.w - 5.0,
                              i.rect.y + i.rect.h - 5.0, 20, NULL) != 0);
    CHECK(ph_fin_ruota(d, v, 30, NULL) != 0);
    CHECK(ph_fin_info(d, v, &i, &e) == 0 && i.rect.w == prima.w && i.rect.h == prima.h &&
          i.stato == PH_FIN_NORMALE);
    /* Spostare, minimizzare, ripristinare e chiudere restano possibili. */
    CHECK(ph_fin_gesto_inizia(d, v, PH_FIN_GESTO_SPOSTA, i.rect.x + 50.0, i.rect.y + 10.0, 40, NULL) == 0);
    CHECK(ph_fin_gesto_muovi(d, 300, 200, 50, NULL) == 0);
    /* Anche trascinata contro il bordo sinistro: nessun aggancio, nessuna anteprima. */
    CHECK(ph_fin_gesto_muovi(d, 5, 200, 60, NULL) == 0);
    CHECK(ph_fin_gesto_anteprima(d, NULL) == PH_FIN_AGG_NESSUNO);
    CHECK(ph_fin_gesto_finisci(d, 5, 200, 70, NULL, NULL) == 0);
    CHECK(ph_fin_info(d, v, &i, &e) == 0 && i.stato == PH_FIN_NORMALE && i.rect.w == prima.w &&
          i.rect.h == prima.h);
    CHECK(ph_fin_minimizza(d, v, 100, NULL) == 0);
    CHECK(ph_fin_ripristina(d, v, 110, NULL) == 0);

    /* Un'app normale: tutti i comandi attivi. */
    PhFinSpec norm = {384, PH_FIN_TITOLO_H + 392, false, false};
    unsigned n = ph_fin_apri(d, "a2", "N", &norm, 200, NULL, &e);
    CHECK(ph_fin_info(d, n, &i, &e) == 0 && !i.solo_verticale);
    CHECK(i.puo_ridimensionare && i.puo_ingrandire && i.puo_agganciare);
    /* Ingrandita e poi l'informazione "solo verticale" arriva in ritardo: torna a
     * forma di telefono come finestra normale; poi, tornando falsa, si puo' di nuovo
     * ridimensionare. */
    CHECK(ph_fin_massimizza_ripristina(d, n, 300, &e) == 0);
    CHECK(ph_fin_imposta_solo_verticale(d, n, true, 400, &e) == 0);
    CHECK(ph_fin_info(d, n, &i, &e) == 0 && i.solo_verticale && i.stato == PH_FIN_NORMALE &&
          i.rect.w == PH_FIN_TEL_W && !i.puo_ingrandire && !i.puo_ridimensionare);
    CHECK(ph_fin_imposta_solo_verticale(d, n, false, 500, &e) == 0);
    CHECK(ph_fin_info(d, n, &i, &e) == 0 && !i.solo_verticale && i.puo_ridimensionare &&
          i.puo_ingrandire);
    /* La richiesta al telefono per la nuova forma parte una volta sola. */
    PhFinRichiesta rich[PH_FIN_MAX];
    size_t k = ph_fin_avanza(d, 900, rich, PH_FIN_MAX);
    CHECK(k >= 1);
    CHECK(ph_fin_avanza(d, 1500, rich, PH_FIN_MAX) == 0);
    ph_fin_libera(d);
}

/* Il CSS del pezzo non ha colori di contenuto scritti a mano: i fondi dei menu sono di vetro CHIARO e un
 * colore chiaro fisso li renderebbe invisibili. Rilevatore comune (shell/prova_css.h). */
static void prova_css_colori(void)
{
    CHECK(ph_prova_css_risorsa("/it/yasde/shell/finestra-app.css", "finestra-app.css", true) == 0);
    CHECK(ph_prova_css_autoprova() == 0);
}

int main(void)
{
    ph_risorse_finestra_app_register_resource();
    if (ph_shell_stile_controlla("/it/yasde/shell/finestra-app.css") != 0) {
        fprintf(stderr, "finestra-app.css: errori di sintassi\n");
        errori++;
    }
    prova_css_colori();
    prova_tasti();
    prova_chiavi();
    prova_spec();
    prova_screenshot();
    prova_pausa();
    prova_titolo();
    prova_rotella();
    prova_compatibilita();
    prova_pizzico();
    prova_zoom_touch();
    prova_specchio_e_bordo();
    prova_aperture();
    prova_riga();
    prova_origine();
    prova_input();
    prova_telefono();
    prova_tablet();
    prova_solo_verticale();
    if (errori == 0)
        printf("prova_finestra_app: tutto bene\n");
    return errori == 0 ? 0 : 1;
}
