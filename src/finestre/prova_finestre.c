/* Prove delle regole delle finestre: pura logica, con i numeri del mockup (6.2).
 * Girano con AddressSanitizer. Finestra host del mockup: 980x584 -> area utile
 * 980x536 (barra 48); schermo intero 1280x800; ingrandita 1280x764. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "finestre/finestre.h"

static int errori;

#define CONTROLLA(cond)                                                              \
    do {                                                                             \
        if (!(cond)) {                                                               \
            fprintf(stderr, "%s:%d: fallito: %s\n", __FILE__, __LINE__, #cond);     \
            errori++;                                                                \
        }                                                                            \
    } while (0)

static PhFinDesktop *nuovo(int l, int a, double s)
{
    PhErrore e;
    PhFinDesktop *d = ph_fin_crea(l, a, s, &e);
    if (!d) fprintf(stderr, "ph_fin_crea: %s\n", e.testo);
    CONTROLLA(d != NULL);
    if (!d) exit(1);
    /* Le prove storiche (numeri del mockup, contenuto = altezza - 70) usano il piede acceso;
     * il piede spento (predefinito del prodotto) ha le sue prove in prova_pie_spento. */
    CONTROLLA(ph_fin_imposta_pie(d, true, 0, NULL) == 0);
    /* Idem per la densita': le prove storiche sono scritte per la regola FISSA (nessun assestamento della misura);
     * la regola automatica ha le sue prove (prova_densita_automatica, prova_assestamento_8, ...). */
    CONTROLLA(ph_fin_imposta_densita_automatica(d, false, 0, NULL) == 0);
    return d;
}

static unsigned apri(PhFinDesktop *d, const char *app, uint64_t t)
{
    PhFinSpec s = ph_fin_spec_mockup(app);
    unsigned id = ph_fin_apri(d, app, app, &s, t, NULL, NULL);
    CONTROLLA(id != 0);
    return id;
}

static PhFinInfo info(const PhFinDesktop *d, unsigned id)
{
    PhFinInfo i;
    memset(&i, 0, sizeof i);
    CONTROLLA(ph_fin_info(d, id, &i, NULL) == 0);
    return i;
}

static bool uguale(PhFinRect r, int x, int y, int w, int h) { return r.x == x && r.y == y && r.w == w && r.h == h; }

/* ---------------------------------------------------------------- */
static void prova_area(void)
{
    PhFinDesktop *d = nuovo(980, 584, 1.0);
    PhFinRect a = ph_fin_area(d);
    CONTROLLA(a.w == 980 && a.h == 536);
    CONTROLLA(ph_fin_imposta_host(d, 1280, 800, 1.0, 0, NULL) == 0); /* schermo intero */
    a = ph_fin_area(d);
    CONTROLLA(a.w == 1280 && a.h == 752);
    CONTROLLA(ph_fin_imposta_host(d, 1280, 764, 1.0, 0, NULL) == 0); /* ingrandita */
    a = ph_fin_area(d);
    CONTROLLA(a.w == 1280 && a.h == 716);
    CONTROLLA(ph_fin_imposta_host(d, 1960, 1168, 2.0, 0, NULL) == 0); /* scala 2: stesse unita' logiche */
    a = ph_fin_area(d);
    CONTROLLA(a.w == 980 && a.h == 536);
    CONTROLLA(ph_fin_imposta_host(d, 0, 100, 1.0, 0, NULL) == -1);
    CONTROLLA(ph_fin_imposta_host(d, 100, 100, 0.0, 0, NULL) == -1);
    CONTROLLA(ph_fin_imposta_host(d, 100, 100, -1.0, 0, NULL) == -1);
    CONTROLLA(ph_fin_imposta_host(d, 100, 100, 0.05, 0, NULL) == -1);      /* scala < 0,1 */
    CONTROLLA(ph_fin_imposta_host(d, 40000, 800, 1.0, 0, NULL) == -1);     /* area logica > 32768 */
    CONTROLLA(ph_fin_imposta_host(d, 800, 40000, 1.0, 0, NULL) == -1);
    CONTROLLA(ph_fin_imposta_host(d, 3000, 800, 0.1, 0, NULL) == 0);       /* 30000 x 8000: ammessa */
    CONTROLLA(ph_fin_imposta_host(d, 3300, 800, 0.1, 0, NULL) == -1);      /* 33000 */
    CONTROLLA(ph_fin_imposta_host(d, 980, 584, 1.0, 0, NULL) == 0);
    CONTROLLA(ph_fin_crea(-5, 100, 1.0, NULL) == NULL);
    ph_fin_libera(d);
}

static void prova_apertura(void)
{
    PhFinDesktop *d = nuovo(980, 584, 1.0);
    bool nuova = false;
    PhFinSpec sn = ph_fin_spec_mockup("note");
    CONTROLLA(sn.w == 400 && sn.h == 290 && !sn.solo_verticale);
    PhFinSpec sm = ph_fin_spec_mockup("msg");
    CONTROLLA(sm.w == 296 && sm.h == 470 && sm.solo_verticale);
    PhFinSpec sx = ph_fin_spec_mockup("sconosciuta");
    CONTROLLA(sx.w == PH_FIN_GENERICA_W && sx.h == PH_FIN_GENERICA_H && !sx.solo_verticale);
    CONTROLLA((sx.w % 8) == 0 && ((sx.h - PH_FIN_TITOLO_H) % 8) == 0); /* a scala 1, piede spento: contenuto multiplo di 8 */

    unsigned a = ph_fin_apri(d, "note", "Note", &sn, 0, &nuova, NULL);
    CONTROLLA(a == 1 && nuova);
    CONTROLLA(uguale(info(d, a).rect, 70, 30, 400, 290));
    CONTROLLA(ph_fin_attiva(d) == a);
    unsigned b = apri(d, "web", 0);
    CONTROLLA(uguale(info(d, b).rect, 110, 60, 540, 340)); /* cascata +40,+30 */
    unsigned c = apri(d, "msg", 0);
    CONTROLLA(uguale(info(d, c).rect, 150, 536 - 470, 296, 470)); /* 90+470 non starebbe: portata dentro */
    CONTROLLA(ph_fin_attiva(d) == c);
    /* l'app "phone" non conta nella cascata e parte in 640,20 */
    unsigned p = apri(d, "phone", 0);
    CONTROLLA(uguale(info(d, p).rect, 640, 20, 280, 500));
    unsigned f = apri(d, "set", 0);
    CONTROLLA(uguale(info(d, f).rect, 190, 120, 360, 400)); /* n = 3 (phone escluso) */
    /* app gia' aperta: stessa finestra, non ne crea un'altra */
    size_t n0 = ph_fin_conta(d);
    CONTROLLA(ph_fin_apri(d, "note", "Note", &sn, 0, &nuova, NULL) == a && !nuova);
    CONTROLLA(ph_fin_conta(d) == n0);
    CONTROLLA(ph_fin_attiva(d) == a);
    unsigned ord[8];
    CONTROLLA(ph_fin_ordine(d, ord, 8) == n0);
    CONTROLLA(ord[n0 - 1] == a);
    CONTROLLA(ph_fin_cerca_app(d, "web") == b && ph_fin_cerca_app(d, "nope") == 0);
    /* argomenti non validi */
    PhErrore e;
    CONTROLLA(ph_fin_apri(d, "", "x", &sn, 0, NULL, &e) == 0);
    CONTROLLA(e.testo[0] != 0);
    CONTROLLA(ph_fin_apri(d, NULL, "x", &sn, 0, NULL, NULL) == 0);
    PhFinSpec zero = { 0, 10, false, false };
    CONTROLLA(ph_fin_apri(d, "zz", "x", &zero, 0, NULL, NULL) == 0);
    /* la sesta finestra "normale" riparte dalla cascata 0 (n % 6) */
    ph_fin_libera(d);
    d = nuovo(980, 584, 1.0);
    char nome[16];
    unsigned id7 = 0;
    for (int i = 0; i < 7; i++) {
        snprintf(nome, sizeof nome, "app%d", i);
        id7 = apri(d, nome, 0);
    }
    CONTROLLA(info(d, id7).rect.x == 70 + 0 * 40 && info(d, id7).rect.y == 30);
    ph_fin_libera(d);
}

static void prova_ciclo_vita(void)
{
    PhFinDesktop *d = nuovo(980, 584, 1.0);
    unsigned a = apri(d, "note", 0), b = apri(d, "web", 0), c = apri(d, "set", 0);
    CONTROLLA(ph_fin_attiva(d) == c);
    /* porta davanti */
    CONTROLLA(ph_fin_porta_davanti(d, a, NULL) == 0);
    unsigned ord[4];
    CONTROLLA(ph_fin_ordine(d, ord, 4) == 3 && ord[0] == b && ord[1] == c && ord[2] == a);
    CONTROLLA(ph_fin_attiva(d) == a);
    /* minimizza: la attiva non c'e' piu', resta nell'ordine */
    CONTROLLA(ph_fin_minimizza(d, a, 0, NULL) == 0);
    CONTROLLA(ph_fin_attiva(d) == 0 && info(d, a).minimizzata && !info(d, a).attiva);
    CONTROLLA(ph_fin_conta(d) == 3);
    CONTROLLA(ph_fin_minimizza(d, a, 0, NULL) == -1);
    CONTROLLA(ph_fin_porta_davanti(d, a, NULL) == -1);
    CONTROLLA(ph_fin_ripristina(d, b, 0, NULL) == -1); /* non e' minimizzata */
    CONTROLLA(ph_fin_ripristina(d, a, 0, NULL) == 0);
    CONTROLLA(ph_fin_attiva(d) == a && !info(d, a).minimizzata);
    /* clic sulla barra: attiva -> minimizza; minimizzata -> ripristina; altrimenti davanti */
    PhFinAzione az = PH_FIN_AZ_NESSUNA;
    CONTROLLA(ph_fin_clic_barra(d, a, 0, &az, NULL) == 0 && az == PH_FIN_AZ_MINIMIZZATA);
    CONTROLLA(info(d, a).minimizzata);
    CONTROLLA(ph_fin_clic_barra(d, a, 0, &az, NULL) == 0 && az == PH_FIN_AZ_RIPRISTINATA);
    CONTROLLA(!info(d, a).minimizzata && ph_fin_attiva(d) == a);
    CONTROLLA(ph_fin_clic_barra(d, b, 0, &az, NULL) == 0 && az == PH_FIN_AZ_DAVANTI);
    CONTROLLA(ph_fin_attiva(d) == b);
    CONTROLLA(ph_fin_clic_barra(d, 999, 0, &az, NULL) == -1);
    /* chiudi: nessuna diventa attiva (mockup); id non riusati */
    CONTROLLA(ph_fin_chiudi(d, b, 0, NULL) == 0);
    CONTROLLA(ph_fin_attiva(d) == 0 && ph_fin_conta(d) == 2);
    CONTROLLA(ph_fin_chiudi(d, b, 0, NULL) == -1);
    unsigned b2 = apri(d, "web", 0);
    CONTROLLA(b2 > c);
    /* massimizza / ripristina: normale */
    PhFinRect prima = info(d, a).rect;
    CONTROLLA(ph_fin_massimizza_ripristina(d, a, 0, NULL) == 0);
    CONTROLLA(info(d, a).stato == PH_FIN_MASSIMIZZATA && uguale(info(d, a).rect, 0, 0, 980, 536));
    CONTROLLA(ph_fin_massimizza_ripristina(d, a, 0, NULL) == 0);
    CONTROLLA(info(d, a).stato == PH_FIN_NORMALE);
    CONTROLLA(uguale(info(d, a).rect, prima.x, prima.y, prima.w, prima.h));
    /* una massimizzata segue il ridimensionamento dell'host */
    CONTROLLA(ph_fin_massimizza_ripristina(d, a, 0, NULL) == 0);
    CONTROLLA(ph_fin_imposta_host(d, 1280, 800, 1.0, 0, NULL) == 0);
    CONTROLLA(uguale(info(d, a).rect, 0, 0, 1280, 752));
    CONTROLLA(ph_fin_imposta_host(d, 980, 584, 1.0, 0, NULL) == 0);
    CONTROLLA(uguale(info(d, a).rect, 0, 0, 980, 536));
    CONTROLLA(ph_fin_massimizza_ripristina(d, a, 0, NULL) == 0);
    /* massimizza una minimizzata: errore */
    CONTROLLA(ph_fin_minimizza(d, a, 0, NULL) == 0);
    CONTROLLA(ph_fin_massimizza_ripristina(d, a, 0, NULL) == -1);
    ph_fin_libera(d);
}

static void prova_solo_verticali(void)
{
    PhFinDesktop *d = nuovo(980, 584, 1.0);
    unsigned m = apri(d, "msg", 0);
    CONTROLLA(info(d, m).solo_verticale);
    PhErrore e;
    /* niente ridimensionamento */
    CONTROLLA(ph_fin_gesto_inizia(d, m, PH_FIN_GESTO_RIDIMENSIONA, 400, 400, 0, &e) == -1 && e.testo[0]);
    CONTROLLA(ph_fin_gesto_finestra(d) == 0);
    /* niente ingrandimento: torna -1 e non cambia nulla (anche con l'area che cambia) */
    PhFinInfo iv = info(d, m);
    CONTROLLA(!iv.puo_ridimensionare && !iv.puo_ingrandire && !iv.puo_agganciare);
    PhFinRect r0 = iv.rect;
    CONTROLLA(ph_fin_massimizza_ripristina(d, m, 0, &e) == -1 && e.testo[0]);
    CONTROLLA(info(d, m).stato == PH_FIN_NORMALE && uguale(info(d, m).rect, r0.x, r0.y, r0.w, r0.h));
    CONTROLLA(ph_fin_imposta_host(d, 1280, 800, 1.0, 0, NULL) == 0); /* schermo intero */
    PhFinRect r = info(d, m).rect;
    CONTROLLA(r.w == 296 && r.h == 470);
    /* una normale ha i tre comandi attivi; minimizzata o in registrazione no */
    unsigned nn = apri(d, "note", 0);
    PhFinInfo in = info(d, nn);
    CONTROLLA(in.puo_ridimensionare && in.puo_ingrandire && in.puo_agganciare);
    CONTROLLA(ph_fin_imposta_registrazione(d, nn, true, 0, NULL) == 0);
    in = info(d, nn);
    CONTROLLA(!in.puo_ridimensionare && !in.puo_ingrandire && !in.puo_agganciare);
    CONTROLLA(ph_fin_imposta_registrazione(d, nn, false, 0, NULL) == 0);
    CONTROLLA(ph_fin_massimizza_ripristina(d, nn, 0, NULL) == 0);
    in = info(d, nn);
    CONTROLLA(!in.puo_ridimensionare && in.puo_ingrandire && in.puo_agganciare); /* massimizzata: si puo' ripristinare */
    CONTROLLA(ph_fin_massimizza_ripristina(d, nn, 0, NULL) == 0);
    CONTROLLA(ph_fin_minimizza(d, nn, 0, NULL) == 0);
    in = info(d, nn);
    CONTROLLA(!in.puo_ridimensionare && !in.puo_ingrandire && !in.puo_agganciare);
    CONTROLLA(ph_fin_chiudi(d, nn, 0, NULL) == 0);
    /* si puo' minimizzare, ripristinare, chiudere */
    CONTROLLA(ph_fin_minimizza(d, m, 0, NULL) == 0 && ph_fin_ripristina(d, m, 0, NULL) == 0);
    /* nessun aggancio per le solo-verticali, ma si sposta */
    CONTROLLA(ph_fin_gesto_inizia(d, m, PH_FIN_GESTO_SPOSTA, 200, 10, 0, NULL) == 0);
    CONTROLLA(ph_fin_gesto_muovi(d, 3, 5, 10, NULL) == 0);
    CONTROLLA(ph_fin_gesto_anteprima(d, NULL) == PH_FIN_AGG_NESSUNO);
    PhFinAggancio ag = PH_FIN_AGG_A_SINISTRA;
    CONTROLLA(ph_fin_gesto_finisci(d, 3, 5, 20, &ag, NULL) == 0 && ag == PH_FIN_AGG_NESSUNO);
    CONTROLLA(info(d, m).stato == PH_FIN_NORMALE && info(d, m).rect.w == 296);
    /* area piccolissima: la finestra fissa si riduce in proporzione, resta dentro */
    CONTROLLA(ph_fin_imposta_host(d, 200, 148, 1.0, 0, NULL) == 0); /* area 200x100 */
    r = info(d, m).rect;
    CONTROLLA(r.w >= 1 && r.h >= 1 && r.h <= 100 && r.w <= 200 && r.x >= 0 && r.y >= 0);
    CONTROLLA(r.w * 470 / 296 >= r.h - 2 && r.w * 470 / 296 <= r.h + 2); /* proporzione conservata */
    CONTROLLA(ph_fin_imposta_host(d, 980, 584, 1.0, 0, NULL) == 0);
    CONTROLLA(info(d, m).rect.w == 296 && info(d, m).rect.h == 470); /* la misura fissa ritorna */
    ph_fin_libera(d);

    /* ---- l'informazione "solo verticale" arriva dopo l'apertura ---- */
    d = nuovo(980, 584, 1.0);
    CONTROLLA(ph_fin_imposta_qualita(d, 450, 160, 2560, 0, NULL) == 0);
    CONTROLLA(ph_fin_imposta_densita_automatica(d, true, 0, NULL) == 0);
    unsigned a1 = apri(d, "web", 0), a2 = apri(d, "set", 0), a3 = apri(d, "note", 0);
    CONTROLLA(ph_fin_massimizza_ripristina(d, a1, 0, NULL) == 0); /* massimizzata */
    PhFinAggancio agg;
    CONTROLLA(ph_fin_gesto_inizia(d, a2, PH_FIN_GESTO_SPOSTA, 100, 40, 0, NULL) == 0);
    CONTROLLA(ph_fin_gesto_finisci(d, 3, 200, 1, &agg, NULL) == 0 && agg == PH_FIN_AGG_A_SINISTRA); /* agganciata */
    PhFinRichiesta rq[4];
    (void)ph_fin_avanza(d, 5000, rq, 4); /* azzera le richieste pendenti */
    CONTROLLA(ph_fin_avanza(d, 5000, rq, 4) == 0);
    /* massimizzata -> telefono 296x472, normale, una sola richiesta dopo 300 ms */
    CONTROLLA(ph_fin_imposta_solo_verticale(d, a1, true, 10000, NULL) == 0);
    CONTROLLA(info(d, a1).solo_verticale && info(d, a1).stato == PH_FIN_NORMALE);
    CONTROLLA(info(d, a1).rect.w == PH_FIN_TEL_W && info(d, a1).rect.h == PH_FIN_TEL_H);
    CONTROLLA(!info(d, a1).puo_ingrandire && !info(d, a1).puo_agganciare && !info(d, a1).puo_ridimensionare);
    CONTROLLA(ph_fin_avanza(d, 10299, rq, 4) == 0);
    CONTROLLA(ph_fin_avanza(d, 10300, rq, 4) == 1 && rq[0].id == a1 && rq[0].w == 296 && rq[0].h == 416); /* contenuto 296x420 (piede acceso) -> utile 296x416 */
    CONTROLLA(ph_fin_avanza(d, 20000, rq, 4) == 0); /* una sola */
    /* agganciata -> telefono normale */
    CONTROLLA(ph_fin_imposta_solo_verticale(d, a2, true, 30000, NULL) == 0);
    CONTROLLA(info(d, a2).stato == PH_FIN_NORMALE && info(d, a2).rect.w == PH_FIN_TEL_W && info(d, a2).rect.h == PH_FIN_TEL_H);
    CONTROLLA(ph_fin_avanza(d, 30300, rq, 4) == 1 && rq[0].id == a2);
    /* stessa informazione: nessun effetto; gesto in corso: finisce */
    CONTROLLA(ph_fin_imposta_solo_verticale(d, a2, true, 40000, NULL) == 0);
    CONTROLLA(ph_fin_avanza(d, 50000, rq, 4) == 0);
    CONTROLLA(ph_fin_gesto_inizia(d, a3, PH_FIN_GESTO_RIDIMENSIONA, 465, 315, 60000, NULL) == 0);
    CONTROLLA(ph_fin_imposta_solo_verticale(d, a3, true, 60001, NULL) == 0);
    CONTROLLA(ph_fin_gesto_finestra(d) == 0);
    CONTROLLA(info(d, a3).rect.w == PH_FIN_TEL_W && info(d, a3).rect.h == PH_FIN_TEL_H);
    /* una finestra larga (non ingrandita) torna a 296x470 */
    unsigned a4 = apri(d, "map", 70000);
    CONTROLLA(ph_fin_gesto_inizia(d, a4, PH_FIN_GESTO_RIDIMENSIONA, 600, 400, 70000, NULL) == 0);
    CONTROLLA(ph_fin_gesto_finisci(d, 900, 450, 70001, NULL, NULL) == 0);
    CONTROLLA(info(d, a4).rect.w > 600);
    CONTROLLA(ph_fin_imposta_solo_verticale(d, a4, true, 70002, NULL) == 0);
    CONTROLLA(info(d, a4).rect.w == PH_FIN_TEL_W && info(d, a4).rect.h == PH_FIN_TEL_H);
    /* ora ingrandire/ridimensionare/agganciare non fanno nulla */
    CONTROLLA(ph_fin_massimizza_ripristina(d, a4, 70003, NULL) == -1);
    CONTROLLA(ph_fin_gesto_inizia(d, a4, PH_FIN_GESTO_RIDIMENSIONA, 1, 1, 70003, NULL) == -1);
    CONTROLLA(ph_fin_gesto_inizia(d, a4, PH_FIN_GESTO_SPOSTA, 100, 40, 70003, NULL) == 0);
    CONTROLLA(ph_fin_gesto_finisci(d, 3, 200, 70004, &agg, NULL) == 0 && agg == PH_FIN_AGG_NESSUNO);
    CONTROLLA(info(d, a4).stato == PH_FIN_NORMALE);
    /* ritorno a falso: si puo' di nuovo ridimensionare, la misura resta */
    CONTROLLA(ph_fin_imposta_solo_verticale(d, a4, false, 70005, NULL) == 0);
    CONTROLLA(!info(d, a4).solo_verticale && info(d, a4).puo_ridimensionare && info(d, a4).puo_ingrandire);
    CONTROLLA(info(d, a4).rect.w == 296);
    CONTROLLA(ph_fin_massimizza_ripristina(d, a4, 70006, NULL) == 0);
    /* area minuscola: la forma di telefono si riduce in proporzione e resta dentro */
    CONTROLLA(ph_fin_imposta_host(d, 200, 148, 1.0, 80000, NULL) == 0);
    CONTROLLA(ph_fin_imposta_solo_verticale(d, a4, true, 80001, NULL) == 0);
    CONTROLLA(info(d, a4).rect.w <= 200 && info(d, a4).rect.h <= 100 && info(d, a4).rect.w >= 1);
    CONTROLLA(ph_fin_imposta_solo_verticale(d, 9999, true, 0, &e) == -1 && e.testo[0]);
    CONTROLLA(ph_fin_imposta_solo_verticale(NULL, 1, true, 0, NULL) == -1);
    ph_fin_libera(d);
}

static void prova_gesti(void)
{
    PhFinDesktop *d = nuovo(980, 584, 1.0);
    unsigned n = apri(d, "note", 0); /* (70,30) 400x290 */
    unsigned w = apri(d, "web", 0);  /* (110,60) 540x340, davanti */
    PhErrore e;
    /* trascinamento: il titolo segue il puntatore, la finestra va davanti */
    CONTROLLA(ph_fin_gesto_inizia(d, n, PH_FIN_GESTO_SPOSTA, 200, 40, 0, NULL) == 0);
    CONTROLLA(ph_fin_gesto_finestra(d) == n && ph_fin_attiva(d) == n);
    CONTROLLA(ph_fin_gesto_inizia(d, w, PH_FIN_GESTO_SPOSTA, 200, 40, 0, &e) == -1); /* uno solo */
    CONTROLLA(ph_fin_gesto_muovi(d, 300, 90, 10, NULL) == 0);
    CONTROLLA(uguale(info(d, n).rect, 170, 80, 400, 290));
    PhFinAggancio ag;
    CONTROLLA(ph_fin_gesto_finisci(d, 300, 90, 20, &ag, NULL) == 0 && ag == PH_FIN_AGG_NESSUNO);
    CONTROLLA(ph_fin_gesto_finestra(d) == 0);
    CONTROLLA(ph_fin_gesto_muovi(d, 1, 1, 30, &e) == -1); /* nessun gesto */

    /* limiti: y >= 0; sopra la barra; titolo sempre raggiungibile */
    CONTROLLA(ph_fin_gesto_inizia(d, n, PH_FIN_GESTO_SPOSTA, 200, 100, 0, NULL) == 0);
    CONTROLLA(ph_fin_gesto_muovi(d, 200, -500, 1, NULL) == 0 && info(d, n).rect.y == 0);
    CONTROLLA(ph_fin_gesto_muovi(d, 200, 5000, 2, NULL) == 0 && info(d, n).rect.y == 536 - PH_FIN_TITOLO_H); /* puo' sporgere in basso: resta la barra del titolo */
    CONTROLLA(ph_fin_gesto_muovi(d, 9000, 100, 3, NULL) == 0 && info(d, n).rect.x == 980 - PH_FIN_VISIBILE_MIN);
    CONTROLLA(ph_fin_gesto_muovi(d, -9000, 100, 4, NULL) == 0 && info(d, n).rect.x == PH_FIN_VISIBILE_MIN - 400);
    ph_fin_gesto_annulla(d, 5);

    /* aggancio a sinistra con anteprima durante il movimento */
    CONTROLLA(ph_fin_gesto_inizia(d, n, PH_FIN_GESTO_SPOSTA, 200, 40, 10, NULL) == 0);
    PhFinRect an;
    CONTROLLA(ph_fin_gesto_anteprima(d, &an) == PH_FIN_AGG_NESSUNO);
    CONTROLLA(ph_fin_gesto_muovi(d, 13, 300, 11, NULL) == 0); /* 13 < 14 */
    CONTROLLA(ph_fin_gesto_anteprima(d, &an) == PH_FIN_AGG_A_SINISTRA && uguale(an, 0, 0, 490, 536));
    CONTROLLA(info(d, n).rect.w == 400 && info(d, n).rect.h == 290); /* spostata, non ridimensionata */
    CONTROLLA(ph_fin_gesto_muovi(d, 14, 300, 12, NULL) == 0); /* 14 non basta */
    CONTROLLA(ph_fin_gesto_anteprima(d, NULL) == PH_FIN_AGG_NESSUNO);
    CONTROLLA(ph_fin_gesto_muovi(d, 5, 300, 13, NULL) == 0);
    CONTROLLA(ph_fin_gesto_finisci(d, 5, 300, 14, &ag, NULL) == 0 && ag == PH_FIN_AGG_A_SINISTRA);
    CONTROLLA(info(d, n).stato == PH_FIN_AGG_SX && uguale(info(d, n).rect, 0, 0, 490, 536));
    CONTROLLA(ph_fin_gesto_anteprima(d, NULL) == PH_FIN_AGG_NESSUNO);

    /* aggancio a destra: x = W/2, w = W/2 */
    CONTROLLA(ph_fin_gesto_inizia(d, w, PH_FIN_GESTO_SPOSTA, 300, 70, 20, NULL) == 0);
    CONTROLLA(ph_fin_gesto_finisci(d, 970, 300, 21, &ag, NULL) == 0 && ag == PH_FIN_AGG_A_DESTRA); /* > 980-14 */
    CONTROLLA(info(d, w).stato == PH_FIN_AGG_DX && uguale(info(d, w).rect, 490, 0, 490, 536));

    /* le due metà seguono l'host: 1280 -> 640 + 640 */
    CONTROLLA(ph_fin_imposta_host(d, 1280, 800, 1.0, 30, NULL) == 0);
    CONTROLLA(uguale(info(d, n).rect, 0, 0, 640, 752) && uguale(info(d, w).rect, 640, 0, 640, 752));
    CONTROLLA(ph_fin_imposta_host(d, 980, 584, 1.0, 31, NULL) == 0);

    /* stacco: torna la misura di prima, con il puntatore alla stessa proporzione */
    CONTROLLA(ph_fin_gesto_inizia(d, n, PH_FIN_GESTO_SPOSTA, 98, 10, 40, NULL) == 0);
    CONTROLLA(info(d, n).stato == PH_FIN_AGG_SX); /* un semplice clic non stacca */
    CONTROLLA(ph_fin_gesto_muovi(d, 300, 200, 41, NULL) == 0);
    CONTROLLA(info(d, n).stato == PH_FIN_NORMALE && info(d, n).rect.w == 400 && info(d, n).rect.h == 290);
    /* 98/490 = 0,2 del titolo -> 80 px dentro i 400: x = 300 - 80 = 220; y = 200 - 10 */
    CONTROLLA(info(d, n).rect.x == 220 && info(d, n).rect.y == 190);
    CONTROLLA(ph_fin_gesto_finisci(d, 300, 200, 42, &ag, NULL) == 0 && ag == PH_FIN_AGG_NESSUNO);

    /* aggancio in alto = massimizza; si ripristina dove era */
    PhFinRect dove = info(d, n).rect;
    CONTROLLA(ph_fin_gesto_inizia(d, n, PH_FIN_GESTO_SPOSTA, dove.x + 50, dove.y + 10, 50, NULL) == 0);
    CONTROLLA(ph_fin_gesto_muovi(d, 400, 9, 51, NULL) == 0);
    CONTROLLA(ph_fin_gesto_anteprima(d, &an) == PH_FIN_AGG_IN_ALTO && uguale(an, 0, 0, 980, 536));
    CONTROLLA(ph_fin_gesto_finisci(d, 400, 9, 52, &ag, NULL) == 0 && ag == PH_FIN_AGG_IN_ALTO);
    CONTROLLA(info(d, n).stato == PH_FIN_MASSIMIZZATA && uguale(info(d, n).rect, 0, 0, 980, 536));
    CONTROLLA(ph_fin_gesto_inizia(d, n, PH_FIN_GESTO_SPOSTA, 400, 9, 53, &e) == -1); /* massimizzata: no */
    CONTROLLA(ph_fin_massimizza_ripristina(d, n, 54, NULL) == 0);
    CONTROLLA(info(d, n).stato == PH_FIN_NORMALE && info(d, n).rect.w == 400 && info(d, n).rect.h == 290);
    CONTROLLA(info(d, n).rect.y == 0); /* aveva y=0 al momento dell'aggancio (puntatore a y=9) */

    /* gesto interrotto: la finestra resta, nessun aggancio */
    CONTROLLA(ph_fin_gesto_inizia(d, n, PH_FIN_GESTO_SPOSTA, 100, 10, 60, NULL) == 0);
    CONTROLLA(ph_fin_gesto_muovi(d, 3, 200, 61, NULL) == 0);
    PhFinRect resto = info(d, n).rect;
    ph_fin_gesto_annulla(d, 62);
    CONTROLLA(ph_fin_gesto_finestra(d) == 0 && info(d, n).stato == PH_FIN_NORMALE);
    CONTROLLA(uguale(info(d, n).rect, resto.x, resto.y, resto.w, resto.h));
    CONTROLLA(ph_fin_gesto_finisci(d, 3, 200, 63, NULL, NULL) == -1);

    /* minimizzata o inesistente: niente gesto */
    CONTROLLA(ph_fin_minimizza(d, w, 70, NULL) == 0);
    CONTROLLA(ph_fin_gesto_inizia(d, w, PH_FIN_GESTO_SPOSTA, 1, 1, 71, NULL) == -1);
    CONTROLLA(ph_fin_gesto_inizia(d, 4242, PH_FIN_GESTO_SPOSTA, 1, 1, 71, NULL) == -1);
    ph_fin_libera(d);
}

static void prova_ridimensiona(void)
{
    PhFinDesktop *d = nuovo(980, 584, 1.0);
    unsigned n = apri(d, "note", 0); /* (70,30) 400x290: angolo in (470,320) */
    CONTROLLA(ph_fin_gesto_inizia(d, n, PH_FIN_GESTO_RIDIMENSIONA, 465, 315, 0, NULL) == 0);
    CONTROLLA(ph_fin_gesto_muovi(d, 565, 365, 1, NULL) == 0);
    CONTROLLA(uguale(info(d, n).rect, 70, 30, 500, 340));
    CONTROLLA(ph_fin_gesto_anteprima(d, NULL) == PH_FIN_AGG_NESSUNO); /* nessun aggancio in ridimensionamento */
    CONTROLLA(ph_fin_gesto_muovi(d, -3000, -3000, 2, NULL) == 0);
    CONTROLLA(uguale(info(d, n).rect, 70, 30, 288, 180)); /* minimo: PH_FIN_MIN_W x PH_FIN_MIN_H */
    CONTROLLA(ph_fin_gesto_muovi(d, 9000, 9000, 3, NULL) == 0);
    CONTROLLA(uguale(info(d, n).rect, 70, 30, 980 - 70, 536 - 30)); /* fino al bordo e alla barra */
    PhFinAggancio ag = PH_FIN_AGG_IN_ALTO;
    CONTROLLA(ph_fin_gesto_finisci(d, 9000, 9000, 4, &ag, NULL) == 0 && ag == PH_FIN_AGG_NESSUNO);
    CONTROLLA(info(d, n).stato == PH_FIN_NORMALE);

    /* ridimensionare una agganciata: parte dalla sua misura e diventa normale */
    PhFinAggancio a2;
    CONTROLLA(ph_fin_gesto_inizia(d, n, PH_FIN_GESTO_SPOSTA, 100, 10, 10, NULL) == 0);
    CONTROLLA(ph_fin_gesto_finisci(d, 2, 200, 11, &a2, NULL) == 0 && a2 == PH_FIN_AGG_A_SINISTRA);
    CONTROLLA(ph_fin_gesto_inizia(d, n, PH_FIN_GESTO_RIDIMENSIONA, 480, 530, 12, NULL) == 0);
    CONTROLLA(info(d, n).stato == PH_FIN_NORMALE && uguale(info(d, n).rect, 0, 0, 490, 536));
    CONTROLLA(ph_fin_gesto_muovi(d, 380, 430, 13, NULL) == 0);
    CONTROLLA(info(d, n).rect.w == 390 && info(d, n).rect.h == 436);
    ph_fin_gesto_annulla(d, 14);
    ph_fin_libera(d);
}

/* Ridimensionare una finestra a cavallo del bordo sinistro non fa sparire il titolo. */
static void prova_ridimensiona_a_cavallo(void)
{
    PhFinDesktop *d = nuovo(980, 584, 1.0);
    unsigned n = apri(d, "web", 0); /* 540x340 */
    CONTROLLA(ph_fin_gesto_inizia(d, n, PH_FIN_GESTO_SPOSTA, 200, 40, 0, NULL) == 0);
    CONTROLLA(ph_fin_gesto_muovi(d, -9000, 40, 1, NULL) == 0);
    ph_fin_gesto_annulla(d, 2);
    CONTROLLA(info(d, n).rect.x == PH_FIN_VISIBILE_MIN - 540);
    CONTROLLA(ph_fin_gesto_inizia(d, n, PH_FIN_GESTO_RIDIMENSIONA, 100, 100, 3, NULL) == 0);
    CONTROLLA(ph_fin_gesto_muovi(d, -5000, 100, 4, NULL) == 0);
    PhFinRect r = info(d, n).rect;
    CONTROLLA(r.x + r.w >= PH_FIN_VISIBILE_MIN && r.w >= PH_FIN_MIN_W);
    ph_fin_gesto_annulla(d, 5);
    ph_fin_libera(d);
}

static void prova_chiusa_durante_gesto(void)
{
    PhFinDesktop *d = nuovo(980, 584, 1.0);
    unsigned n = apri(d, "note", 0), w = apri(d, "web", 0);
    CONTROLLA(ph_fin_gesto_inizia(d, n, PH_FIN_GESTO_SPOSTA, 100, 40, 0, NULL) == 0);
    CONTROLLA(ph_fin_chiudi(d, n, 5, NULL) == 0);
    CONTROLLA(ph_fin_gesto_finestra(d) == 0);
    CONTROLLA(ph_fin_gesto_muovi(d, 300, 300, 6, NULL) == -1);
    CONTROLLA(ph_fin_gesto_finisci(d, 300, 300, 7, NULL, NULL) == -1);
    /* altra finestra: il gesto continua */
    CONTROLLA(ph_fin_gesto_inizia(d, w, PH_FIN_GESTO_RIDIMENSIONA, 600, 380, 8, NULL) == 0);
    CONTROLLA(ph_fin_chiudi(d, n, 9, NULL) == -1);
    CONTROLLA(ph_fin_gesto_finestra(d) == w);
    /* minimizzare la finestra in gesto lo annulla */
    CONTROLLA(ph_fin_minimizza(d, w, 10, NULL) == 0);
    CONTROLLA(ph_fin_gesto_finestra(d) == 0);
    /* nessuna richiesta per una minimizzata, anche dopo molto tempo */
    PhFinRichiesta rq[4];
    CONTROLLA(ph_fin_avanza(d, 10000, rq, 4) == 0);
    ph_fin_libera(d);
}

static void prova_host_ridimensionato(void)
{
    PhFinDesktop *d = nuovo(1280, 800, 1.0); /* area 1280x752 */
    unsigned n = apri(d, "web", 0), s = apri(d, "set", 0);
    CONTROLLA(ph_fin_gesto_inizia(d, n, PH_FIN_GESTO_SPOSTA, 200, 70, 0, NULL) == 0);
    CONTROLLA(ph_fin_gesto_muovi(d, 1100, 600, 1, NULL) == 0);
    CONTROLLA(ph_fin_gesto_finisci(d, 1100, 600, 2, NULL, NULL) == 0);
    /* restringe a 600x400: area 600x352; le finestre sono riportate solo quanto basta (titolo e 96 px visibili) */
    CONTROLLA(ph_fin_imposta_host(d, 600, 400, 1.0, 3, NULL) == 0);
    PhFinInfo i = info(d, n);
    CONTROLLA(i.rect.y >= 0 && i.rect.y <= 352 - PH_FIN_TITOLO_H && i.rect.x <= 600 - PH_FIN_VISIBILE_MIN && i.rect.x + i.rect.w >= PH_FIN_VISIBILE_MIN && i.rect.w <= 600 && i.rect.h <= 352);
    i = info(d, s);
    CONTROLLA(i.rect.y >= 0 && i.rect.y <= 352 - PH_FIN_TITOLO_H && i.rect.x <= 600 - PH_FIN_VISIBILE_MIN && i.rect.x + i.rect.w >= PH_FIN_VISIBILE_MIN && i.rect.w <= 600 && i.rect.h <= 352);
    /* area piu' piccola di ogni misura minima */
    CONTROLLA(ph_fin_imposta_host(d, 100, 60, 1.0, 4, NULL) == 0); /* area 100x12 */
    i = info(d, n);
    CONTROLLA(i.rect.w >= 1 && i.rect.h >= 1 && i.rect.w <= 100 && i.rect.h <= 12 && i.rect.y >= 0 && i.rect.y + (i.rect.h < PH_FIN_TITOLO_H ? i.rect.h : PH_FIN_TITOLO_H) <= 12);
    /* area 1x1 (host piu' piccolo della barra) */
    CONTROLLA(ph_fin_imposta_host(d, 1, 1, 1.0, 5, NULL) == 0);
    PhFinRect a = ph_fin_area(d);
    CONTROLLA(a.w == 1 && a.h == 1);
    i = info(d, n);
    CONTROLLA(i.rect.w == 1 && i.rect.h == 1 && i.rect.x == 0 && i.rect.y == 0);
    int fw, fh;
    CONTROLLA(ph_fin_misura_flusso(d, n, &fw, &fh, NULL) == 0 && fw == 64 && fh == 64);
    /* gesto e ridimensionamento con area minuscola non rompono i limiti */
    CONTROLLA(ph_fin_gesto_inizia(d, n, PH_FIN_GESTO_RIDIMENSIONA, 0, 0, 6, NULL) == 0);
    CONTROLLA(ph_fin_gesto_muovi(d, 50, 50, 7, NULL) == 0);
    CONTROLLA(info(d, n).rect.w == 1 && info(d, n).rect.h == 1);
    ph_fin_gesto_annulla(d, 8);
    CONTROLLA(ph_fin_imposta_host(d, 980, 584, 1.0, 9, NULL) == 0);
    ph_fin_libera(d);
}

static void prova_venti_finestre(void)
{
    PhFinDesktop *d = nuovo(980, 584, 1.0);
    unsigned ids[PH_FIN_MAX + 2];
    char nome[24];
    int k = 0;
    for (; k < 20; k++) {
        snprintf(nome, sizeof nome, "app%d", k);
        ids[k] = apri(d, nome, (uint64_t)k);
    }
    CONTROLLA(ph_fin_conta(d) == 20 && ph_fin_attiva(d) == ids[19]);
    for (int i = 0; i < 20; i++)
        for (int j = i + 1; j < 20; j++) CONTROLLA(ids[i] != ids[j]);
    unsigned ord[32];
    CONTROLLA(ph_fin_ordine(d, ord, 32) == 20);
    for (int i = 0; i < 20; i++) {
        CONTROLLA(ord[i] == ids[i]);
        PhFinRect r = info(d, ids[i]).rect;
        CONTROLLA(r.x >= 0 && r.y >= 0 && r.x + r.w <= 980 && r.y + r.h <= 536);
    }
    /* porta davanti la prima */
    CONTROLLA(ph_fin_porta_davanti(d, ids[0], NULL) == 0);
    CONTROLLA(ph_fin_ordine(d, ord, 32) == 20 && ord[19] == ids[0] && ord[0] == ids[1]);
    /* si arriva al massimo e poi si rifiuta senza danni */
    for (; k < PH_FIN_MAX; k++) {
        snprintf(nome, sizeof nome, "app%d", k);
        CONTROLLA(apri(d, nome, 0) != 0);
    }
    PhErrore e;
    CONTROLLA(ph_fin_apri(d, "una-in-piu", "x", NULL, 0, NULL, &e) == 0 && e.testo[0]);
    CONTROLLA(ph_fin_conta(d) == PH_FIN_MAX);
    for (size_t i = 0; i < PH_FIN_MAX; i++) CONTROLLA(ph_fin_chiudi(d, ph_fin_ordine(d, ord, 1) ? ord[0] : 0, 0, NULL) == 0);
    CONTROLLA(ph_fin_conta(d) == 0 && ph_fin_attiva(d) == 0);
    ph_fin_libera(d);
}

/* ---------------------------------------------------------------- */
static void prova_flusso(void)
{
    int w, h;
    /* mockup: densita' 420 / 160 = 2,625; note: contenuto 400 x (290-70) = 400x220 */
    CONTROLLA(ph_fin_calcola_flusso(400, 220, 420, 160, 2560, &w, &h, NULL) == 0);
    CONTROLLA(w == 1048 && h == 576); /* 1050 -> 1048, 577,5 -> 576 */
    /* messaggi 296x(470-70=400) */
    CONTROLLA(ph_fin_calcola_flusso(296, 400, 420, 160, 2560, &w, &h, NULL) == 0);
    CONTROLLA(w == 776 && h == 1048);
    /* sempre multipli di 8 e dentro [64, tetto] */
    for (int a = 1; a < 3000; a += 37)
        for (int b = 1; b < 3000; b += 41) {
            CONTROLLA(ph_fin_calcola_flusso(a, b, 450, 160, 2560, &w, &h, NULL) == 0);
            CONTROLLA(w % 8 == 0 && h % 8 == 0 && w >= 64 && h >= 64 && w <= 2560 && h <= 2560);
        }
    /* minimo 64 */
    CONTROLLA(ph_fin_calcola_flusso(3, 3, 160, 160, 2560, &w, &h, NULL) == 0 && w == 64 && h == 64);
    /* tetto con proporzioni conservate: 4000x2000 a densita' uguale, tetto 2560 -> 2560x1280 */
    CONTROLLA(ph_fin_calcola_flusso(4000, 2000, 160, 160, 2560, &w, &h, NULL) == 0 && w == 2560 && h == 1280);
    CONTROLLA(ph_fin_calcola_flusso(2000, 4000, 160, 160, 2560, &w, &h, NULL) == 0 && w == 1280 && h == 2560);
    /* tetto non multiplo di 8: arrotondato per difetto */
    CONTROLLA(ph_fin_calcola_flusso(9000, 100, 160, 160, 1001, &w, &h, NULL) == 0 && w == 1000);
    /* argomenti non validi */
    CONTROLLA(ph_fin_calcola_flusso(0, 10, 160, 160, 2560, &w, &h, NULL) == -1);
    CONTROLLA(ph_fin_calcola_flusso(10, 10, 0, 160, 2560, &w, &h, NULL) == -1);
    CONTROLLA(ph_fin_calcola_flusso(10, 10, 49, 160, 2560, &w, &h, NULL) == -1);
    CONTROLLA(ph_fin_calcola_flusso(10, 10, 2001, 160, 2560, &w, &h, NULL) == -1);
    CONTROLLA(ph_fin_calcola_flusso(10, 10, 160, -1, 2560, &w, &h, NULL) == -1);
    CONTROLLA(ph_fin_calcola_flusso(10, 10, 160, 160, 2560, NULL, &h, NULL) == -1);

    /* dal desktop, con le tre manopole: la scala della shell NON cambia il flusso */
    PhFinDesktop *d = nuovo(980, 584, 1.0);
    CONTROLLA(ph_fin_imposta_qualita(d, 420, 160, 2560, 0, NULL) == 0);
    CONTROLLA(ph_fin_imposta_densita_automatica(d, false, 0, NULL) == 0); /* regola vecchia: densita' fissa */
    unsigned n = apri(d, "note", 0);
    CONTROLLA(ph_fin_misura_flusso(d, n, &w, &h, NULL) == 0 && w == 1048 && h == 576);
    CONTROLLA(info(d, n).flusso_w == 1048 && info(d, n).flusso_h == 576);
    CONTROLLA(ph_fin_imposta_host(d, 1960, 1168, 2.0, 0, NULL) == 0); /* stessa area logica, scala 2 */
    CONTROLLA(ph_fin_misura_flusso(d, n, &w, &h, NULL) == 0 && w == 1048 && h == 576);
    CONTROLLA(ph_fin_imposta_host(d, 1470, 876, 1.5, 0, NULL) == 0);
    CONTROLLA(ph_fin_misura_flusso(d, n, &w, &h, NULL) == 0 && w == 1048 && h == 576);
    CONTROLLA(ph_fin_imposta_qualita(d, 0, 160, 2560, 0, NULL) == -1);
    CONTROLLA(ph_fin_imposta_qualita(d, 49, 160, 2560, 0, NULL) == -1);    /* densita' 50-2000 */
    CONTROLLA(ph_fin_imposta_qualita(d, 2001, 160, 2560, 0, NULL) == -1);
    CONTROLLA(ph_fin_imposta_qualita(d, 420, 49, 2560, 0, NULL) == -1);
    CONTROLLA(ph_fin_imposta_qualita(d, 50, 2000, 2560, 0, NULL) == 0);
    CONTROLLA(ph_fin_imposta_qualita(d, 420, 160, 2560, 0, NULL) == 0);
    CONTROLLA(ph_fin_imposta_qualita(d, 420, 160, 10, 0, NULL) == -1);
    CONTROLLA(ph_fin_misura_flusso(d, 777, &w, &h, NULL) == -1);
    ph_fin_libera(d);

    /* CASO REALE DA NON SBAGLIARE. Monitor 1920x1200, finestra interna a tutto
     * schermo, densita' del telefono 450. Area 1920x1152, contenuto 1920x1100:
     *   1920*450/160 = 5400 px, 1100*450/160 = 3094 px  -> oltre il tetto
     *   -> 2560x1464 con la densita' che RESTA 450  = 910x520 dp (un tablet).
     * Sbagliato (versione 1): abbassare la densita' fino a far stare 2558 px,
     * cioe' 2558 dp a 160 dpi = un muro. */
    d = nuovo(1920, 1200, 1.0);
    CONTROLLA(ph_fin_imposta_qualita(d, 450, 160, 2560, 0, NULL) == 0);
    /* densita' FISSA (scelta dell'utente): regola della versione 1; la regola automatica ha prova_densita_automatica */
    CONTROLLA(ph_fin_imposta_densita_automatica(d, false, 0, NULL) == 0);
    unsigned g = apri(d, "web", 0);
    CONTROLLA(ph_fin_massimizza_ripristina(d, g, 0, NULL) == 0);
    CONTROLLA(uguale(info(d, g).rect, 0, 0, 1920, 1152));
    CONTROLLA(ph_fin_misura_flusso(d, g, &w, &h, NULL) == 0);
    CONTROLLA(w == 2560 && h == 1440); /* contenuto 1920x1082: 2560 x 1442,7 -> 1440 */
    double dp_w = w * 160.0 / 450.0, dp_h = h * 160.0 / 450.0;
    CONTROLLA(dp_w > 900 && dp_w < 920 && dp_h > 510 && dp_h < 530);
    CONTROLLA(dp_w < 1000 && dp_h < 600);        /* paragonabile a un tablet... */
    CONTROLLA(dp_w < 2558 / 2.0);                /* ...e non al muro da 2558 dp */
    /* stesso risultato su monitor HiDPI 3840x2400 a scala 2 */
    CONTROLLA(ph_fin_imposta_host(d, 3840, 2400, 2.0, 0, NULL) == 0);
    CONTROLLA(ph_fin_misura_flusso(d, g, &w, &h, NULL) == 0 && w == 2560 && h == 1440);
    /* proporzioni conservate al tetto: contenuto 1920x1082 -> 1,777 */
    CONTROLLA((double)w / h > 1.77 && (double)w / h < 1.79);
    /* un tetto piu' basso (parametro) */
    CONTROLLA(ph_fin_imposta_qualita(d, 450, 160, 1920, 0, NULL) == 0);
    CONTROLLA(ph_fin_misura_flusso(d, g, &w, &h, NULL) == 0 && w == 1920 && h == 1080); /* 3043,1*1920/5400 = 1081,9 -> 1080 */
    /* regola AUTOMATICA sulla stessa finestra: modalita' tablet, flusso 1:1 (1920 x 1082 -> 1080),
     * densita' 160 (= riferimento della shell a scala 1: 1 dp = 1 px logico), il telefono vede 1920 x 1080 dp */
    CONTROLLA(ph_fin_imposta_qualita(d, 450, 160, 2560, 0, NULL) == 0);
    CONTROLLA(ph_fin_imposta_host(d, 1920, 1200, 1.0, 0, NULL) == 0);
    CONTROLLA(ph_fin_imposta_densita_automatica(d, true, 0, NULL) == 0);
    int dens = 0;
    CONTROLLA(ph_fin_misura_flusso_densita(d, g, &w, &h, &dens, NULL) == 0 && w == 1920 && h == 1080 && dens == 160); /* contenuto 1920x1082 (piede acceso) -> utile 1920x1080 */
    CONTROLLA(info(d, g).tablet && info(d, g).densita == 160);
    ph_fin_libera(d);
}

static void prova_assestato(void)
{
    PhFinDesktop *d = nuovo(980, 584, 1.0);
    CONTROLLA(ph_fin_imposta_qualita(d, 420, 160, 2560, 0, NULL) == 0);
    CONTROLLA(ph_fin_imposta_densita_automatica(d, false, 0, NULL) == 0); /* qui si prova l'assestamento, non la densita' */
    unsigned n = apri(d, "note", 0); /* flusso 1048x624 data all'apertura */
    PhFinRichiesta rq[4];
    CONTROLLA(ph_fin_avanza(d, 5000, rq, 4) == 0); /* niente di nuovo: nessuna richiesta */

    /* ridimensiona: durante il gesto nessuna richiesta, anche dopo secondi */
    CONTROLLA(ph_fin_gesto_inizia(d, n, PH_FIN_GESTO_RIDIMENSIONA, 465, 315, 10000, NULL) == 0);
    CONTROLLA(ph_fin_gesto_muovi(d, 565, 365, 10100, NULL) == 0); /* 500x340 */
    CONTROLLA(ph_fin_avanza(d, 10200, rq, 4) == 0);
    CONTROLLA(ph_fin_gesto_muovi(d, 585, 385, 10200, NULL) == 0); /* 520x360 */
    CONTROLLA(ph_fin_avanza(d, 15000, rq, 4) == 0); /* il gesto dura ancora */
    CONTROLLA(ph_fin_gesto_finisci(d, 585, 385, 20000, NULL, NULL) == 0);
    CONTROLLA(ph_fin_avanza(d, 20000, rq, 4) == 0);
    CONTROLLA(ph_fin_avanza(d, 20299, rq, 4) == 0);
    /* un nuovo cambio azzera l'attesa */
    CONTROLLA(ph_fin_imposta_host(d, 980, 584, 1.0, 20200, NULL) == 0); /* l'area e' la stessa: la misura del flusso non cambia, l'attesa non si azzera ne' si anticipa */
    CONTROLLA(ph_fin_avanza(d, 20300, rq, 4) == 1);
    /* 520x(360-70=290) a 2,625 = 1365 x 761,25 -> 1368 x 760 */
    CONTROLLA(rq[0].id == n && rq[0].w == 1368 && rq[0].h == 760);
    int w, h;
    CONTROLLA(ph_fin_misura_flusso(d, n, &w, &h, NULL) == 0 && w == rq[0].w && h == rq[0].h);
    CONTROLLA(ph_fin_avanza(d, 30000, rq, 4) == 0); /* una volta sola */

    /* cambio durante l'attesa: riparte da capo */
    CONTROLLA(ph_fin_gesto_inizia(d, n, PH_FIN_GESTO_RIDIMENSIONA, 585, 385, 40000, NULL) == 0);
    CONTROLLA(ph_fin_gesto_finisci(d, 485, 385, 40010, NULL, NULL) == 0); /* 420 x 360 */
    CONTROLLA(ph_fin_avanza(d, 40200, rq, 4) == 0);
    CONTROLLA(ph_fin_gesto_inizia(d, n, PH_FIN_GESTO_RIDIMENSIONA, 485, 385, 40250, NULL) == 0);
    CONTROLLA(ph_fin_gesto_finisci(d, 505, 385, 40260, NULL, NULL) == 0); /* 440 x 360: cambio nuovo */
    CONTROLLA(ph_fin_avanza(d, 40500, rq, 4) == 0); /* 240 ms dopo l'ultimo cambio */
    CONTROLLA(ph_fin_avanza(d, 40559, rq, 4) == 0);
    CONTROLLA(ph_fin_avanza(d, 40560, rq, 4) == 1 && rq[0].w == 1152 && rq[0].h == 760);
    /* 440*2,625 = 1155 -> 1152; 290*2,625 = 761,25 -> 760 */

    /* tornare alla misura gia' chiesta durante l'attesa: nessuna richiesta */
    CONTROLLA(ph_fin_gesto_inizia(d, n, PH_FIN_GESTO_RIDIMENSIONA, 505, 385, 50000, NULL) == 0);
    CONTROLLA(ph_fin_gesto_muovi(d, 705, 385, 50010, NULL) == 0);
    CONTROLLA(ph_fin_gesto_finisci(d, 505, 385, 50020, NULL, NULL) == 0);
    CONTROLLA(ph_fin_avanza(d, 60000, rq, 4) == 0);

    /* solo spostare: la misura del flusso non cambia, nessuna richiesta */
    CONTROLLA(ph_fin_gesto_inizia(d, n, PH_FIN_GESTO_SPOSTA, 100, 40, 61000, NULL) == 0);
    CONTROLLA(ph_fin_gesto_finisci(d, 200, 100, 61010, NULL, NULL) == 0);
    CONTROLLA(ph_fin_avanza(d, 70000, rq, 4) == 0);

    /* massimizzare cambia la misura: richiesta dopo 300 ms; ripristinare la riporta */
    CONTROLLA(ph_fin_massimizza_ripristina(d, n, 80000, NULL) == 0);
    CONTROLLA(ph_fin_avanza(d, 80299, rq, 4) == 0);
    CONTROLLA(ph_fin_avanza(d, 80300, rq, 4) == 1);
    /* contenuto 980x466 -> 2572,5x1223,25 -> tetto -> 2560x1217,3 -> 2560x1216 */
    CONTROLLA(rq[0].w == 2560 && rq[0].h == 1216);
    /* minimizzata: nessuna richiesta, e al ripristino nessuna se la misura e' quella chiesta */
    CONTROLLA(ph_fin_minimizza(d, n, 81000, NULL) == 0);
    CONTROLLA(ph_fin_avanza(d, 90000, rq, 4) == 0);
    CONTROLLA(ph_fin_ripristina(d, n, 91000, NULL) == 0);
    CONTROLLA(ph_fin_avanza(d, 92000, rq, 4) == 0);
    /* cambio di qualita' (densita'): rifa' la misura, con attesa */
    CONTROLLA(ph_fin_imposta_qualita(d, 320, 160, 2560, 93000, NULL) == 0);
    CONTROLLA(ph_fin_avanza(d, 93100, rq, 4) == 0);
    CONTROLLA(ph_fin_avanza(d, 93300, rq, 4) == 1);
    /* orologio che torna indietro: non esplode e non emette */
    CONTROLLA(ph_fin_imposta_qualita(d, 160, 160, 2560, 100000, NULL) == 0);
    CONTROLLA(ph_fin_avanza(d, 50, rq, 4) == 0);
    /* max = 0: nessuna scrittura */
    CONTROLLA(ph_fin_avanza(d, 200000, rq, 0) == 0);
    CONTROLLA(ph_fin_avanza(d, 200000, rq, 4) == 1);
    ph_fin_libera(d);
}

/* ---------------------------------------------------------------- */
static void prova_input(void)
{
    PhFinDesktop *d = nuovo(980, 584, 1.0);
    CONTROLLA(ph_fin_imposta_qualita(d, 420, 160, 2560, 0, NULL) == 0);
    unsigned n = apri(d, "note", 0); /* (70,30) 400x290: titolo y 30..80, contenuto y 80..300, piede 300..320 */
    PhFinColpo c;
    ph_fin_trova(d, 5, 5, &c);
    CONTROLLA(c.id == 0 && c.zona == PH_FIN_ZONA_FUORI);
    ph_fin_trova(d, 200, 55, &c);
    CONTROLLA(c.id == n && c.zona == PH_FIN_ZONA_TITOLO);
    /* pulsanti da destra (30 px, 4 di distanza, 14 dal bordo destro 470): chiudi 426..456, ingrandisci 392..422,
     * minimizza 358..388, altri (tre puntini) 324..354, registra 290..320, screenshot 256..286;
     * indietro 92..122 (22 dal bordo sinistro 70); y 40..70 (centrati nel titolo 30..80) */
    ph_fin_trova(d, 440, 55, &c);
    CONTROLLA(c.zona == PH_FIN_ZONA_PULSANTE && c.pulsante == PH_FIN_PULS_CHIUDI);
    ph_fin_trova(d, 410, 55, &c);
    CONTROLLA(c.zona == PH_FIN_ZONA_PULSANTE && c.pulsante == PH_FIN_PULS_INGRANDISCI);
    ph_fin_trova(d, 370, 55, &c);
    CONTROLLA(c.pulsante == PH_FIN_PULS_MINIMIZZA);
    ph_fin_trova(d, 340, 55, &c);
    CONTROLLA(c.pulsante == PH_FIN_PULS_ALTRI);
    ph_fin_trova(d, 300, 55, &c);
    CONTROLLA(c.pulsante == PH_FIN_PULS_REGISTRA);
    ph_fin_trova(d, 270, 55, &c);
    CONTROLLA(c.pulsante == PH_FIN_PULS_SCREENSHOT);
    ph_fin_trova(d, 255, 55, &c); /* a sinistra dello screenshot: titolo */
    CONTROLLA(c.zona == PH_FIN_ZONA_TITOLO);
    ph_fin_trova(d, 100, 55, &c);
    CONTROLLA(c.pulsante == PH_FIN_PULS_INDIETRO);
    ph_fin_trova(d, 424, 55, &c); /* tra i pulsanti (gap di 4 px) */
    CONTROLLA(c.zona == PH_FIN_ZONA_TITOLO && c.pulsante == PH_FIN_PULS_NESSUNO);
    ph_fin_trova(d, 457, 55, &c); /* 14 px di margine a destra */
    CONTROLLA(c.zona == PH_FIN_ZONA_TITOLO && c.pulsante == PH_FIN_PULS_NESSUNO);
    ph_fin_trova(d, 440, 39, &c); /* sopra il pulsante (i pulsanti stanno in y 40..70) */
    CONTROLLA(c.zona == PH_FIN_ZONA_TITOLO && c.pulsante == PH_FIN_PULS_NESSUNO);
    ph_fin_trova(d, 440, 70, &c); /* sotto il pulsante */
    CONTROLLA(c.zona == PH_FIN_ZONA_TITOLO && c.pulsante == PH_FIN_PULS_NESSUNO);
    ph_fin_trova(d, 121, 55, &c); /* ultimo punto di indietro */
    CONTROLLA(c.pulsante == PH_FIN_PULS_INDIETRO);
    ph_fin_trova(d, 91, 55, &c); /* 22 px di margine a sinistra */
    CONTROLLA(c.zona == PH_FIN_ZONA_TITOLO);
    /* angolo 18x18 in basso a destra: x 452..470, y 302..320 */
    ph_fin_trova(d, 465, 315, &c);
    CONTROLLA(c.zona == PH_FIN_ZONA_ANGOLO && c.id == n);
    ph_fin_trova(d, 300, 310, &c);
    CONTROLLA(c.zona == PH_FIN_ZONA_PIE);
    ph_fin_trova(d, 470, 320, &c); /* subito fuori */
    CONTROLLA(c.id == 0);
    ph_fin_trova(d, 70, 30, &c);
    CONTROLLA(c.id == n);

    /* contenuto senza video dichiarato: valido = false */
    ph_fin_trova(d, 270, 190, &c);
    CONTROLLA(c.zona == PH_FIN_ZONA_CONTENUTO && !c.valido && c.video_w == 0);
    PhFinRect cr;
    CONTROLLA(ph_fin_contenuto(d, n, &cr, NULL) == 0 && uguale(cr, 70, 80, 400, 220));
    /* video dichiarato = misura attuale: coordinate = x * larghezza video / larghezza disegnata */
    CONTROLLA(ph_fin_dichiara_video(d, n, 1048, 576, NULL) == 0);
    ph_fin_trova(d, 270, 190, &c); /* centro del contenuto */
    CONTROLLA(c.valido && c.vx == 524.0 && c.vy == 288.0 && c.video_w == 1048 && c.video_h == 576);
    ph_fin_trova(d, 70, 80, &c);
    CONTROLLA(c.valido && c.vx == 0.0 && c.vy == 0.0);
    ph_fin_trova(d, 469.999, 299.999, &c); /* ultimo punto del contenuto: mai oltre il video */
    CONTROLLA(c.zona == PH_FIN_ZONA_CONTENUTO && c.valido && c.vx < 1048.0 && c.vy < 576.0 && c.vx > 1047.0);

    /* scalatura transitoria: la finestra e' stata ridimensionata a 600x470 (contenuto 600x400)
     * ma il video dichiarato e' ancora 1048x576 (piu' piccolo del disegno): le coordinate
     * restano proporzionali al rettangolo DISEGNATO. */
    CONTROLLA(ph_fin_gesto_inizia(d, n, PH_FIN_GESTO_RIDIMENSIONA, 465, 315, 0, NULL) == 0);
    CONTROLLA(ph_fin_gesto_finisci(d, 665, 495, 1, NULL, NULL) == 0);
    CONTROLLA(ph_fin_contenuto(d, n, &cr, NULL) == 0 && uguale(cr, 70, 80, 600, 400));
    ph_fin_trova(d, 70 + 300, 80 + 200, &c);
    CONTROLLA(c.valido && c.vx == 524.0 && c.vy == 288.0 && c.video_w == 1048 && c.video_h == 576);
    ph_fin_trova(d, 70 + 150, 80 + 100, &c);
    CONTROLLA(c.vx == 262.0 && c.vy == 144.0);
    /* video PIU' GRANDE del disegno (finestra rimpicciolita): 400x220 disegnato, video 2000x1000 */
    CONTROLLA(ph_fin_gesto_inizia(d, n, PH_FIN_GESTO_RIDIMENSIONA, 665, 495, 2, NULL) == 0);
    CONTROLLA(ph_fin_gesto_finisci(d, 465, 315, 3, NULL, NULL) == 0);
    CONTROLLA(ph_fin_dichiara_video(d, n, 2000, 1000, NULL) == 0);
    ph_fin_trova(d, 270, 190, &c);
    CONTROLLA(c.vx == 1000.0 && c.vy == 500.0 && c.video_w == 2000 && c.video_h == 1000);
    CONTROLLA(ph_fin_dichiara_video(d, n, 100, 0, NULL) == -1);
    CONTROLLA(ph_fin_dichiara_video(d, n, -1, -1, NULL) == -1);
    CONTROLLA(ph_fin_dichiara_video(d, 999, 10, 10, NULL) == -1);

    /* profondita': la finestra davanti vince; minimizzata non riceve */
    unsigned w = apri(d, "web", 0); /* (110,60) 540x340, copre il punto (270,181) */
    ph_fin_trova(d, 270, 181, &c);
    CONTROLLA(c.id == w && c.zona == PH_FIN_ZONA_CONTENUTO && !c.valido);
    CONTROLLA(ph_fin_minimizza(d, w, 0, NULL) == 0);
    ph_fin_trova(d, 270, 181, &c);
    CONTROLLA(c.id == n);
    CONTROLLA(ph_fin_porta_davanti(d, n, NULL) == 0);
    CONTROLLA(ph_fin_ripristina(d, w, 0, NULL) == 0);
    CONTROLLA(ph_fin_porta_davanti(d, n, NULL) == 0);
    ph_fin_trova(d, 270, 181, &c);
    CONTROLLA(c.id == n);
    /* massimizzata: niente angolo; verticale: niente angolo, colonna */
    CONTROLLA(ph_fin_massimizza_ripristina(d, n, 0, NULL) == 0);
    ph_fin_trova(d, 975, 530, &c);
    CONTROLLA(c.zona != PH_FIN_ZONA_ANGOLO && c.id == n);
    unsigned m = apri(d, "msg", 0); /* solo verticale: non si ingrandisce, nessun angolo */
    CONTROLLA(ph_fin_massimizza_ripristina(d, m, 0, NULL) == -1);
    PhFinRect rm = info(d, m).rect;
    CONTROLLA(rm.w == 296 && rm.h == 470);
    ph_fin_trova(d, rm.x + rm.w / 2, rm.y + rm.h / 2, &c);
    CONTROLLA(c.id == m && c.zona == PH_FIN_ZONA_CONTENUTO);
    ph_fin_trova(d, rm.x + rm.w - 3, rm.y + rm.h - 3, &c);
    CONTROLLA(c.id == m && c.zona != PH_FIN_ZONA_ANGOLO);
    ph_fin_trova(d, rm.x - 1, rm.y + 100, &c); /* subito fuori: la massimizzata sotto */
    CONTROLLA(c.id == n);
    /* punti strani */
    ph_fin_trova(d, 0.0 / 0.0, 1, &c);
    CONTROLLA(c.id == 0);
    ph_fin_trova(NULL, 1, 1, &c);
    CONTROLLA(c.id == 0);
    ph_fin_libera(d);
}

static void prova_ricordi(void)
{
    PhFinDesktop *d = nuovo(980, 584, 1.0);
    ph_fin_riapri_dove_eri(d, true);
    unsigned n = apri(d, "note", 0);
    unsigned m = apri(d, "msg", 0);
    CONTROLLA(ph_fin_gesto_inizia(d, n, PH_FIN_GESTO_SPOSTA, 100, 40, 0, NULL) == 0);
    CONTROLLA(ph_fin_gesto_finisci(d, 300, 140, 1, NULL, NULL) == 0); /* 270,130 */
    CONTROLLA(ph_fin_gesto_inizia(d, n, PH_FIN_GESTO_RIDIMENSIONA, 665, 477, 2, NULL) == 0);
    CONTROLLA(ph_fin_gesto_finisci(d, 565, 427, 3, NULL, NULL) == 0);
    PhFinRect r = info(d, n).rect;
    PhFinRicordo mem[8];
    size_t k = ph_fin_esporta(d, mem, 8);
    CONTROLLA(k == 2);
    size_t kn = strcmp(mem[0].app, "note") == 0 ? 0 : 1; /* l'ordine segue la profondita' */
    CONTROLLA(strcmp(mem[kn].app, "note") == 0 && mem[kn].x == r.x && mem[kn].y == r.y && mem[kn].w == r.w && mem[kn].h == r.h);
    /* chiusa: il ricordo resta; massimizzata/agganciata: si ricorda la normale */
    CONTROLLA(ph_fin_massimizza_ripristina(d, n, 4, NULL) == 0);
    CONTROLLA(ph_fin_chiudi(d, n, 5, NULL) == 0);
    CONTROLLA(ph_fin_chiudi(d, m, 5, NULL) == 0);
    PhFinRicordo mem2[8];
    k = ph_fin_esporta(d, mem2, 8);
    CONTROLLA(k == 2);
    /* riapre dove era, con la misura di allora */
    unsigned n2 = apri(d, "note", 6);
    CONTROLLA(uguale(info(d, n2).rect, r.x, r.y, r.w, r.h));
    /* un ricordo importato piu' piccolo dei minimi viene portato ai minimi */
    PhFinDesktop *d3 = nuovo(980, 584, 1.0);
    ph_fin_riapri_dove_eri(d3, true);
    PhFinRicordo piccolo;
    memset(&piccolo, 0, sizeof piccolo);
    strcpy(piccolo.app, "web");
    piccolo.x = 10; piccolo.y = 10; piccolo.w = 5; piccolo.h = 7;
    CONTROLLA(ph_fin_importa(d3, &piccolo, 1) == 1);
    unsigned wp = apri(d3, "web", 0);
    CONTROLLA(info(d3, wp).rect.w == PH_FIN_MIN_W && info(d3, wp).rect.h == PH_FIN_MIN_H);
    ph_fin_libera(d3);
    /* una solo-verticale: posizione ricordata, misura fissa */
    unsigned m2 = apri(d, "msg", 6);
    CONTROLLA(info(d, m2).rect.w == 296 && info(d, m2).rect.h == 470 && info(d, m2).rect.x == 110);
    /* senza l'opzione si ignora; export/import in un altro desktop */
    PhFinDesktop *d2 = nuovo(980, 584, 1.0);
    CONTROLLA(ph_fin_importa(d2, mem2, k) == 2);
    unsigned a = apri(d2, "note", 0);
    CONTROLLA(uguale(info(d2, a).rect, 70, 30, 400, 290)); /* opzione spenta */
    ph_fin_libera(d2);
    d2 = nuovo(980, 584, 1.0);
    ph_fin_riapri_dove_eri(d2, true);
    CONTROLLA(ph_fin_importa(d2, mem2, k) == 2);
    a = apri(d2, "note", 0);
    CONTROLLA(uguale(info(d2, a).rect, r.x, r.y, r.w, r.h));
    /* valori assurdi scartati, ricordi enormi portati dentro l'area */
    PhFinRicordo cattivi[4];
    memset(cattivi, 0, sizeof cattivi);
    strcpy(cattivi[0].app, "x"); cattivi[0].w = 0; cattivi[0].h = 10;                    /* misura nulla */
    cattivi[1].w = 10; cattivi[1].h = 10;                                                  /* nome vuoto */
    memset(cattivi[2].app, 'a', sizeof cattivi[2].app);                                    /* senza terminatore */
    cattivi[2].w = 10; cattivi[2].h = 10;
    strcpy(cattivi[3].app, "enorme"); cattivi[3].x = -50000; cattivi[3].y = 90000;
    cattivi[3].w = 100000; cattivi[3].h = 100000;
    CONTROLLA(ph_fin_importa(d2, cattivi, 4) == 1);
    unsigned e = apri(d2, "enorme", 0);
    PhFinRect re = info(d2, e).rect;
    CONTROLLA(re.x >= 0 && re.y >= 0 && re.x + re.w <= 980 && re.y + re.h <= 536 && re.w >= 1 && re.h >= 1);
    CONTROLLA(ph_fin_importa(d2, NULL, 5) == 0);
    /* piu' ricordi del massimo: ne tiene PH_FIN_MEM_MAX */
    PhFinRicordo molti[PH_FIN_MEM_MAX + 10];
    memset(molti, 0, sizeof molti);
    for (int i = 0; i < PH_FIN_MEM_MAX + 10; i++) {
        snprintf(molti[i].app, sizeof molti[i].app, "r%d", i);
        molti[i].w = 300; molti[i].h = 300;
    }
    CONTROLLA(ph_fin_importa(d2, molti, PH_FIN_MEM_MAX + 10) == PH_FIN_MEM_MAX);
    PhFinRicordo fuori[PH_FIN_MEM_MAX + 5];
    CONTROLLA(ph_fin_esporta(d2, fuori, 3) == 3);
    ph_fin_libera(d2);
    ph_fin_libera(d);
}



/* Piede tecnico spento (impostazione predefinita del prodotto): il contenuto usa tutta
 * l'altezza sotto il titolo (si tolgono 50 px, non 70), nessuna zona PIE, l'angolo di
 * ridimensionamento e' in basso a destra della finestra. */
static void prova_pie_spento(void)
{
    PhErrore e;
    PhFinDesktop *d = ph_fin_crea(980, 584, 1.0, &e);
    CONTROLLA(d != NULL);
    if (!d) return;
    CONTROLLA(ph_fin_imposta_qualita(d, 420, 160, 2560, 0, NULL) == 0);
    CONTROLLA(ph_fin_imposta_densita_automatica(d, false, 0, NULL) == 0); /* regola vecchia: densita' fissa */
    unsigned n = apri(d, "note", 0); /* (70,30) 400x290 */
    PhFinRect cr;
    CONTROLLA(ph_fin_contenuto(d, n, &cr, NULL) == 0 && uguale(cr, 70, 80, 400, 240));
    int w, h;
    /* 400 x 240 a 2,625 = 1050 x 630 -> 1048 x 632 (con il piede era 1048 x 576) */
    CONTROLLA(ph_fin_misura_flusso(d, n, &w, &h, NULL) == 0 && w == 1048 && h == 632);
    PhFinColpo c;
    ph_fin_trova(d, 300, 310, &c); /* dove con il piede c'era la zona PIE */
    CONTROLLA(c.id == n && c.zona == PH_FIN_ZONA_CONTENUTO);
    ph_fin_trova(d, 469, 319, &c);
    CONTROLLA(c.zona == PH_FIN_ZONA_ANGOLO);
    ph_fin_trova(d, 452, 302, &c);
    CONTROLLA(c.zona == PH_FIN_ZONA_ANGOLO);
    ph_fin_trova(d, 451, 310, &c);
    CONTROLLA(c.zona == PH_FIN_ZONA_CONTENUTO);
    CONTROLLA(ph_fin_dichiara_video(d, n, 1048, 632, NULL) == 0);
    ph_fin_trova(d, 270, 200, &c); /* centro: 80 + 120 */
    CONTROLLA(c.valido && c.vx == 524.0 && c.vy == 316.0);
    ph_fin_trova(d, 469.999, 319.999, &c);
    CONTROLLA(c.zona == PH_FIN_ZONA_ANGOLO);
    /* massimizzata: contenuto = 980 x (536-50) */
    CONTROLLA(ph_fin_massimizza_ripristina(d, n, 0, NULL) == 0);
    CONTROLLA(ph_fin_contenuto(d, n, &cr, NULL) == 0 && uguale(cr, 0, 50, 980, 486));
    ph_fin_trova(d, 979, 535, &c);
    CONTROLLA(c.zona == PH_FIN_ZONA_CONTENUTO && c.vx < 1048.0 && c.vy < 632.0);
    CONTROLLA(ph_fin_massimizza_ripristina(d, n, 0, NULL) == 0);
    /* accendere il piede cambia la misura: richiesta dopo l'assestamento */
    PhFinRichiesta rq[4];
    CONTROLLA(ph_fin_imposta_pie(d, true, 1000, NULL) == 0);
    CONTROLLA(ph_fin_avanza(d, 1299, rq, 4) == 0);
    CONTROLLA(ph_fin_avanza(d, 1300, rq, 4) == 1 && rq[0].w == 1048 && rq[0].h == 576);
    ph_fin_trova(d, 300, 310, &c);
    CONTROLLA(c.zona == PH_FIN_ZONA_PIE);
    CONTROLLA(ph_fin_imposta_pie(d, false, 2000, NULL) == 0);
    CONTROLLA(ph_fin_imposta_pie(NULL, true, 0, &e) == -1);
    /* ruota senza piede: contenuto 400x240 -> 240x400, ma la larghezza minima e' 288: finestra 288 x 450 */
    CONTROLLA(ph_fin_ruota(d, n, 3000, NULL) == 0);
    CONTROLLA(uguale(info(d, n).rect, 70, 30, 288, 450));
    /* il tetto di 2560 sul monitor 1920x1200: contenuto 1920 x 1102 (non 1082) */
    ph_fin_libera(d);
    d = ph_fin_crea(1920, 1200, 1.0, NULL);
    CONTROLLA(d != NULL);
    if (!d) return;
    CONTROLLA(ph_fin_imposta_qualita(d, 450, 160, 2560, 0, NULL) == 0);
    CONTROLLA(ph_fin_imposta_densita_automatica(d, false, 0, NULL) == 0);
    unsigned g = apri(d, "web", 0);
    CONTROLLA(ph_fin_massimizza_ripristina(d, g, 0, NULL) == 0);
    CONTROLLA(ph_fin_misura_flusso(d, g, &w, &h, NULL) == 0);
    CONTROLLA(w == 2560 && h == 1472); /* fissa: 1102*2,8125 = 3099,4; 2560/5400 * 3099,4 = 1469,4 -> 1472 */
    CONTROLLA(w * 160.0 / 450.0 < 1000 && h * 160.0 / 450.0 < 600);
    /* automatica: tablet, flusso 1:1 1920 x 1102 -> utile 1920 x 1096, densita' 160 */
    CONTROLLA(ph_fin_imposta_densita_automatica(d, true, 0, NULL) == 0);
    int dn = 0;
    CONTROLLA(ph_fin_misura_flusso_densita(d, g, &w, &h, &dn, NULL) == 0 && w == 1920 && h == 1096 && dn == 160);
    ph_fin_libera(d);
}


/* ---- Densita' effettiva automatica (tipo DeX: flusso 1:1 coi pixel fisici, densita' = rif*scala), vedi misura_di ---- */
typedef struct { int w, h, dens; bool tablet; int cw, ch, uw, uh; } Mis;

/* Una finestra normale di contenuto richiesto cw x ch (logici, piede spento: finestra alta ch+50) su un
 * desktop di logicale hw x hh (host fisico = logicale * scala), densita' telefono 450, tetto 2560. In automatica il
 * contenuto si assesta al multiplo di 8 pixel fisici piu' vicino (m.cw, m.ch dopo l'assestamento). */
static Mis una_finestra(int hw, int hh, double scala, int cw, int ch, bool automatica)
{
    Mis m = { 0, 0, 0, false, 0, 0, 0, 0 };
    PhFinDesktop *d = ph_fin_crea((int)(hw * scala), (int)(hh * scala), scala, NULL);
    CONTROLLA(d != NULL);
    if (!d) return m;
    CONTROLLA(ph_fin_imposta_qualita(d, 450, 160, 2560, 0, NULL) == 0);
    CONTROLLA(ph_fin_imposta_densita_automatica(d, automatica, 0, NULL) == 0);
    PhFinSpec sp = { cw, ch + PH_FIN_TITOLO_H, false, false };
    unsigned id = ph_fin_apri(d, "x", "x", &sp, 0, NULL, NULL);
    CONTROLLA(id != 0);
    PhFinRect c;
    CONTROLLA(ph_fin_contenuto(d, id, &c, NULL) == 0);
    m.cw = c.w;
    m.ch = c.h;
    if (automatica) { /* al massimo ~4 px fisici di scostamento per lato */
        { int sp = (int)ceil(4.0 / scala) + 1; CONTROLLA(abs(c.w - cw) <= sp && abs(c.h - ch) <= sp); }
    } else {
        CONTROLLA(c.w == cw && c.h == ch);
    }
    CONTROLLA(ph_fin_misura_flusso_densita(d, id, &m.w, &m.h, &m.dens, NULL) == 0);
    PhFinInfo in = info(d, id);
    m.tablet = in.tablet;
    m.uw = in.utile_w;
    m.uh = in.utile_h;
    if (automatica) { /* flusso = contenuto utile esatto, multiplo di 8, vicino al contenuto vero (resto < 8 px fisici) */
        CONTROLLA(m.uw % 8 == 0 && m.uh % 8 == 0);
        CONTROLLA(m.uw <= c.w * scala + 0.51 && m.uw > c.w * scala - 7.51);
        CONTROLLA(m.uh <= c.h * scala + 0.51 && m.uh > c.h * scala - 7.51);
        if (m.uw <= 2560 && m.uh <= 2560) CONTROLLA(m.w == m.uw && m.h == m.uh);
    } else {
        CONTROLLA(m.uw == 0 && m.uh == 0);
    }
    ph_fin_libera(d);
    return m;
}

static void prova_densita_automatica(void)
{
    Mis m;
    /* Una sola regola per tutte le finestre: flusso = contenuto utile (pixel FISICI, multipli di 8), densita' 160 a scala 1.
     * 380x800 a scala 1: il contenuto si assesta a 376 o 384 (a pari distanza) -> flusso 376x800 o 384x800. */
    m = una_finestra(1280, 1000, 1.0, 380, 800, true);
    CONTROLLA(m.dens == 160 && (m.w == 376 || m.w == 384) && m.h == 800 && !m.tablet);
    /* contenuto del mockup (note 400x258 a piede spento): 400 x 256 */
    m = una_finestra(980, 584, 1.0, 400, 258, true);
    CONTROLLA(m.dens == 160 && m.w == 400 && m.h == 256 && !m.tablet);
    /* 2000x764 logici, scala 1: contenuto 2000x768 (764 e' a meta' strada fra 760 e 768: il bersaglio e' il multiplo di 8 piu' vicino,
     * a pari distanza il maggiore), densita' 160, flusso 1:1 2000x768: il telefono vede 2000 x 768 dp */
    m = una_finestra(2560, 1440, 1.0, 2000, 764, true);
    CONTROLLA(m.tablet && m.dens == 160 && m.w == 2000 && m.h == 768 && m.cw == 2000 && m.ch == 768);
    /* stessa finestra con densita' FISSA (scelta dell'utente): regola vecchia, nessun assestamento, 450 dpi, 5625x2149 -> tetto -> 2560x976 */
    m = una_finestra(2560, 1440, 1.0, 2000, 764, false);
    CONTROLLA(m.dens == 450 && m.w == 2560 && m.h == 976 && m.ch == 764);
    m = una_finestra(1280, 1000, 1.0, 380, 800, false); /* e la finestra piccola con la regola vecchia: 1072x2248 a 450 */
    CONTROLLA(m.dens == 450 && m.w == 1072 && m.h == 2248);

    /* scala 1,5, contenuto 1333x483 logici: assestato a 1333x485 (fisici 1999,5x727,5 -> utile 2000x728), densita' 240 */
    m = una_finestra(2560, 1440, 1.5, 1333, 483, true);
    CONTROLLA(!m.tablet && m.dens == 240 && m.w == 2000 && m.h == 728 && m.cw == 1333 && m.ch == 485);
    /* scala 2, contenuto 1000x620 logici (fisici 2000x1240, gia' multipli di 8): densita' 320 */
    m = una_finestra(2560, 1440, 2.0, 1000, 620, true);
    CONTROLLA(m.tablet && m.dens == 320 && m.w == 2000 && m.h == 1240 && m.cw == 1000 && m.ch == 620);
    /* scala 1,5, 2000x764 logici (assestato a 2000x763: fisici 3000x1144,5 -> utile 3000x1144, oltre il tetto): flusso 2560x976 (riduzione
     * proporzionale), k = 1,28 -> densita' ceil20(204,8) = 220 */
    m = una_finestra(2560, 1440, 1.5, 2000, 764, true);
    CONTROLLA(m.tablet && m.dens == 220 && m.w == 2560 && m.h == 976 && m.uw == 3000 && m.uh == 1144);
    /* scala 1,25 (senza arrivare al tetto: 2500 fisici): densita' 160*1,25 = 200, flusso = contenuto utile */
    m = una_finestra(2560, 1440, 1.25, 2000, 764, true);
    CONTROLLA(m.tablet && m.dens == 200 && m.w == m.uw && m.h == m.uh);
    /* 700x700: assestata a 696 o 704 per lato (scala 1) -> densita' 160; a scala 1,5 -> 699x699 (fisici 1048,5, utile 1048x1048), densita' 240 */
    m = una_finestra(2560, 1440, 1.0, 700, 700, true);
    CONTROLLA(m.tablet && m.dens == 160 && (m.w == 696 || m.w == 704) && m.w == m.h);
    m = una_finestra(2560, 1440, 1.5, 700, 700, true);
    CONTROLLA(m.tablet && m.dens == 240 && m.w == 1048 && m.h == 1048 && m.cw == 699);
    m = una_finestra(2560, 1440, 1.0, 700, 590, true);
    CONTROLLA(!m.tablet && m.dens == 160 && m.h == 592);
    /* larga e bassa 2000x500: niente soglie, stessa regola: 2000 x (496 o 504) a 160 */
    m = una_finestra(2560, 1440, 1.0, 2000, 500, true);
    CONTROLLA(!m.tablet && m.dens == 160 && m.w == 2000 && (m.h == 496 || m.h == 504));
    /* a meta' schermo (1280x800): contenuto 640x696 (finestra 746 su un'area di 752) -> 640x696 a 160 */
    m = una_finestra(1280, 800, 1.0, 640, 696, true);
    CONTROLLA(m.tablet && m.dens == 160 && m.w == 640 && m.h == 696);
    /* molto piccola e molto grande, quattro scale: flusso = contenuto utile, densita' = ceil20(160*scala) (nel tetto) */
    {
        static const double sc[] = { 1.0, 1.25, 1.5, 2.0 };
        for (int k = 0; k < 4; k++)
            for (int cw = 290; cw <= 1250; cw += 97) /* sotto PH_FIN_MIN_W (288) il contenuto non scende */
                for (int ch = 150; ch <= 640; ch += 89) {
                    m = una_finestra(1280, 700, sc[k], cw, ch, true);
                    int atteso = (int)(ceil(160 * sc[k] / 20.0 - 1e-9) * 20);
                    CONTROLLA(m.dens == atteso);
                    CONTROLLA(m.w >= 64 && m.h >= 64 && m.w <= 2560 && m.h <= 2560);
                }
    }

    {
        PhFinDesktop *d = nuovo(2560, 1440, 1.0);
        ph_fin_imposta_pie(d, false, 0, NULL);
        CONTROLLA(ph_fin_imposta_densita_automatica(d, true, 0, NULL) == 0);
        CONTROLLA(ph_fin_imposta_qualita(d, 450, 160, 2560, 0, NULL) == 0);
        int w, h, dn;
        /* solo-verticale (stessa regola): 296x470 (contenuto 296x420 -> utile 416), sempre */
        unsigned mv = apri(d, "msg", 0);
        CONTROLLA(ph_fin_misura_flusso_densita(d, mv, &w, &h, &dn, NULL) == 0 && dn == 160 && w == 296 && h == 416); /* forma fissa: nessun assestamento, utile 296x416 */
        CONTROLLA(ph_fin_massimizza_ripristina(d, mv, 0, NULL) == -1); /* non si ingrandisce: la misura non cambia */
        CONTROLLA(ph_fin_misura_flusso_densita(d, mv, &w, &h, &dn, NULL) == 0 && dn == 160 && w == 296 && h == 416);
        /* lo specchio non ha densita' ne' misura */
        PhFinSpec sp = { 400, 300, false, true };
        unsigned sq = ph_fin_apri(d, "specchio", "S", &sp, 0, NULL, NULL);
        CONTROLLA(sq != 0 && info(d, sq).densita == 0 && info(d, sq).flusso_w == 0 && !info(d, sq).tablet);
        CONTROLLA(ph_fin_misura_flusso_densita(d, sq, &w, &h, &dn, NULL) == -1);
        ph_fin_libera(d);
    }

    /* telefono con densita' piu' bassa di rif*scala (scala 2: 320): vince "mai sopra la densita' del telefono" */
    {
        PhFinDesktop *d = ph_fin_crea(2560, 1440, 2.0, NULL); /* area logica 1280x672 */
        CONTROLLA(d != NULL);
        if (d) {
            CONTROLLA(ph_fin_imposta_qualita(d, 300, 160, 2560, 0, NULL) == 0);
            PhFinSpec sp = { 600, 450, false, false }; /* contenuto 600x400, fisici 1200x800 */
            unsigned id = ph_fin_apri(d, "x", "x", &sp, 0, NULL, NULL);
            int w, h, dn;
            CONTROLLA(ph_fin_misura_flusso_densita(d, id, &w, &h, &dn, NULL) == 0 && dn == 300 && w == 1200 && h == 800);
            CONTROLLA(ph_fin_imposta_qualita(d, 450, 160, 2560, 1, NULL) == 0);
            CONTROLLA(ph_fin_misura_flusso_densita(d, id, &w, &h, &dn, NULL) == 0 && dn == 320 && w == 1200 && h == 800);
            ph_fin_libera(d);
        }
    }

    /* il ridimensionamento non cambia la densita' (nessuna soglia) ma la misura si, e la richiesta porta la densita' */
    {
        PhFinDesktop *d = ph_fin_crea(2560, 1440, 1.0, NULL);
        CONTROLLA(d != NULL);
        if (d) {
            CONTROLLA(ph_fin_imposta_qualita(d, 450, 160, 2560, 0, NULL) == 0);
            PhFinSpec sp = { 1000, 650, false, false }; /* contenuto 1000x600 (titolo 50) */
            unsigned id = ph_fin_apri(d, "x", "x", &sp, 0, NULL, NULL);
            CONTROLLA(info(d, id).tablet && info(d, id).densita == 160);
            CONTROLLA(ph_fin_gesto_inizia(d, id, PH_FIN_GESTO_RIDIMENSIONA, 1070, 680, 10, NULL) == 0);
            CONTROLLA(ph_fin_gesto_muovi(d, 1070, 658, 11, NULL) == 0); /* contenuto 578 */
            CONTROLLA(!info(d, id).tablet && info(d, id).densita == 160); /* l'informazione non ha isteresi */
            CONTROLLA(ph_fin_gesto_muovi(d, 1070, 680, 12, NULL) == 0);
            CONTROLLA(info(d, id).tablet);
            CONTROLLA(ph_fin_gesto_finisci(d, 1070, 658, 20, NULL, NULL) == 0);
            PhFinRichiesta rq[2];
            CONTROLLA(ph_fin_avanza(d, 319, rq, 2) == 0);
            CONTROLLA(ph_fin_avanza(d, 320, rq, 2) == 1 && rq[0].id == id && rq[0].densita == 160);
            CONTROLLA(rq[0].w == 1000 && rq[0].h == 576); /* 578 -> 576 */
            ph_fin_libera(d);
        }
    }
}


/* Cambia SOLO la densita' (la misura del flusso resta uguale): una richiesta dopo 300 ms, con la densita' nuova.
 * Controlla il confronto sulla densita' in ph_fin_avanza. Verifica a mano: senza `bers_d == rich_d` fallisce. */
static void prova_solo_densita(void)
{
    PhFinDesktop *d = ph_fin_crea(2560, 1440, 1.0, NULL);
    CONTROLLA(d != NULL);
    if (!d) return;
    CONTROLLA(ph_fin_imposta_qualita(d, 450, 160, 2560, 0, NULL) == 0);
    PhFinSpec sp = { 2000, 814, false, false }; /* contenuto 2000x764 -> assestato 2000x768: tablet, densita' 160, flusso 2000x768 */
    unsigned id = ph_fin_apri(d, "x", "x", &sp, 0, NULL, NULL);
    CONTROLLA(id != 0 && info(d, id).tablet && info(d, id).densita == 160);
    PhFinRichiesta rq[2];
    CONTROLLA(ph_fin_avanza(d, 10000, rq, 2) == 0);
    /* telefono a 140 dpi: piu' basso di rif*scala = 160: vince "mai sopra la densita' del telefono" */
    CONTROLLA(ph_fin_imposta_qualita(d, 140, 160, 2560, 20000, NULL) == 0);
    CONTROLLA(info(d, id).densita == 140 && info(d, id).flusso_w == 2000 && info(d, id).flusso_h == 768);
    CONTROLLA(ph_fin_avanza(d, 20299, rq, 2) == 0);
    CONTROLLA(ph_fin_avanza(d, 20300, rq, 2) == 1);
    CONTROLLA(rq[0].id == id && rq[0].densita == 140 && rq[0].w == 2000 && rq[0].h == 768);
    CONTROLLA(ph_fin_avanza(d, 30000, rq, 2) == 0);
    ph_fin_libera(d);
}


/* ---- Assestamento del contenuto al multiplo di 8 pixel fisici (flusso 1:1, nitido) ---- */
static void controlla_utile(PhFinDesktop *d, unsigned id, double s, int riga)
{
    PhFinInfo in = info(d, id);
    PhFinRect c;
    CONTROLLA(ph_fin_contenuto(d, id, &c, NULL) == 0);
    int w, h;
    CONTROLLA(ph_fin_misura_flusso(d, id, &w, &h, NULL) == 0);
    int ok = in.utile_w % 8 == 0 && in.utile_h % 8 == 0 && fabs(c.w * s - in.utile_w) <= 1.0 &&
             fabs(c.h * s - in.utile_h) <= 1.0 && w == in.utile_w && h == in.utile_h;
    if (!ok) fprintf(stderr, "riga %d: scala %g contenuto %dx%d utile %dx%d flusso %dx%d\n", riga, s, c.w, c.h, in.utile_w, in.utile_h, w, h);
    CONTROLLA(ok);
}

static void prova_assestamento_8(void)
{
    static const double sc[] = { 1.0, 1.25, 1.5, 2.0 };
    for (int k = 0; k < 4; k++) {
        double s = sc[k];
        PhFinDesktop *d = ph_fin_crea((int)(1280 * s), (int)(800 * s), s, NULL);
        CONTROLLA(d != NULL);
        if (!d) continue;
        CONTROLLA(ph_fin_imposta_qualita(d, 450, 160, 2560, 0, NULL) == 0);
        PhFinSpec sp = { 501, 307, false, false };
        unsigned id = ph_fin_apri(d, "x", "x", &sp, 0, NULL, NULL);
        controlla_utile(d, id, s, __LINE__); /* apertura */
        /* ridimensionamento: durante il gesto nessuna richiesta; a gesto finito il contenuto si assesta */
        static const int dx[] = { 13, -37, 101, -3, 250 };
        static const int dy[] = { 7, 29, -41, 11, 90 };
        for (int i = 0; i < 5; i++) {
            PhFinRect r = info(d, id).rect;
            CONTROLLA(ph_fin_gesto_inizia(d, id, PH_FIN_GESTO_RIDIMENSIONA, r.x + r.w - 3, r.y + r.h - 3, 100 + i, NULL) == 0);
            CONTROLLA(ph_fin_gesto_muovi(d, r.x + r.w - 3 + dx[i], r.y + r.h - 3 + dy[i], 101 + i, NULL) == 0);
            CONTROLLA(ph_fin_gesto_finisci(d, r.x + r.w - 3 + dx[i], r.y + r.h - 3 + dy[i], 102 + i, NULL, NULL) == 0);
            controlla_utile(d, id, s, __LINE__);
            PhFinRect r2 = info(d, id).rect;
            CONTROLLA(r2.w >= PH_FIN_MIN_W && r2.h >= PH_FIN_MIN_H && r2.x + r2.w <= 1280 && r2.y + r2.h <= 752);
        }
        /* la richiesta al telefono porta esattamente il contenuto utile */
        PhFinRichiesta rq[2];
        CONTROLLA(ph_fin_avanza(d, 100000, rq, 2) == 1); /* una richiesta, con il contenuto utile esatto */
        CONTROLLA(rq[0].id == id && rq[0].w == info(d, id).utile_w && rq[0].h == info(d, id).utile_h);
        /* cambio di area e di scala */
        CONTROLLA(ph_fin_imposta_host(d, (int)(1000 * s), (int)(700 * s), s, 200000, NULL) == 0);
        controlla_utile(d, id, s, __LINE__);
        CONTROLLA(ph_fin_imposta_host(d, (int)(1500 * sc[(k + 1) % 4]), (int)(900 * sc[(k + 1) % 4]), sc[(k + 1) % 4], 200001, NULL) == 0);
        controlla_utile(d, id, sc[(k + 1) % 4], __LINE__);
        CONTROLLA(ph_fin_imposta_host(d, (int)(1280 * s), (int)(800 * s), s, 200002, NULL) == 0);
        /* rotazione e piede */
        CONTROLLA(ph_fin_ruota(d, id, 300000, NULL) == 0);
        controlla_utile(d, id, s, __LINE__);
        CONTROLLA(ph_fin_imposta_pie(d, true, 300001, NULL) == 0);
        controlla_utile(d, id, s, __LINE__);
        CONTROLLA(ph_fin_imposta_pie(d, false, 300002, NULL) == 0);
        controlla_utile(d, id, s, __LINE__);
        /* ingrandita e ripristinata: dopo il ripristino il contenuto e' ancora assestato */
        CONTROLLA(ph_fin_massimizza_ripristina(d, id, 400000, NULL) == 0);
        CONTROLLA(ph_fin_massimizza_ripristina(d, id, 400001, NULL) == 0);
        controlla_utile(d, id, s, __LINE__);
        ph_fin_libera(d);
    }

    /* ingrandita: il rettangolo e' imposto, il contenuto resta quello e il resto (< 8 px fisici) e' margine */
    {
        PhFinDesktop *d = ph_fin_crea(1003, 700 + 48, 1.0, NULL); /* area 1003x700 */
        CONTROLLA(d != NULL);
        if (!d) return;
        PhFinSpec sp = { 500, 300, false, false };
        unsigned id = ph_fin_apri(d, "x", "x", &sp, 0, NULL, NULL);
        CONTROLLA(ph_fin_massimizza_ripristina(d, id, 1, NULL) == 0);
        PhFinRect c;
        CONTROLLA(ph_fin_contenuto(d, id, &c, NULL) == 0 && c.w == 1003 && c.h == 650);
        PhFinInfo in = info(d, id);
        CONTROLLA(in.utile_w == 1000 && in.utile_h == 648 && in.flusso_w == 1000 && in.flusso_h == 648);
        /* l'immagine occupa solo il contenuto utile: i punti nel margine non sono nel video */
        CONTROLLA(ph_fin_dichiara_video(d, id, 1000, 648, NULL) == 0);
        PhFinColpo col;
        ph_fin_trova(d, c.x + 500, c.y + 324, &col);
        CONTROLLA(col.zona == PH_FIN_ZONA_CONTENUTO && col.valido && col.vx == 500.0 && col.vy == 324.0);
        ph_fin_trova(d, c.x + 1001, c.y + 10, &col); /* margine a destra */
        CONTROLLA(col.zona == PH_FIN_ZONA_CONTENUTO && !col.valido && col.id == id);
        ph_fin_trova(d, c.x + 10, c.y + 649, &col); /* margine in basso */
        CONTROLLA(col.zona == PH_FIN_ZONA_CONTENUTO && !col.valido);
        ph_fin_trova(d, c.x + 999.9, c.y + 647.9, &col);
        CONTROLLA(col.valido && col.vx < 1000.0 && col.vy < 648.0);
        ph_fin_libera(d);
    }

    /* non si assestano: solo-verticali (forma fissa), densita' fissa, specchio, finestre in registrazione */
    {
        PhFinDesktop *d = ph_fin_crea(1280, 800, 1.0, NULL);
        CONTROLLA(d != NULL);
        if (!d) return;
        PhFinSpec sv = ph_fin_spec_mockup("msg"); /* 296x470: contenuto 296x420, non multiplo di 8 */
        unsigned v = ph_fin_apri(d, "msg", "m", &sv, 0, NULL, NULL);
        CONTROLLA(info(d, v).rect.w == 296 && info(d, v).rect.h == 470 && info(d, v).utile_h == 416);
        PhFinSpec sp = { 501, 307, false, false };
        unsigned id = ph_fin_apri(d, "x", "x", &sp, 0, NULL, NULL);
        CONTROLLA(ph_fin_imposta_registrazione(d, id, true, 1, NULL) == 0);
        PhFinRect prima = info(d, id).rect;
        CONTROLLA(ph_fin_imposta_pie(d, true, 2, NULL) == 0); /* il contenuto cambia di 20 px ma la finestra no */
        CONTROLLA(uguale(info(d, id).rect, prima.x, prima.y, prima.w, prima.h));
        CONTROLLA(ph_fin_imposta_pie(d, false, 3, NULL) == 0);
        CONTROLLA(ph_fin_imposta_registrazione(d, id, false, 4, NULL) == 0);
        CONTROLLA(ph_fin_imposta_densita_automatica(d, false, 5, NULL) == 0); /* densita' fissa: nessun assestamento, nessun utile */
        PhFinRect r5 = info(d, id).rect;
        CONTROLLA(info(d, id).utile_w == 0 && info(d, id).utile_h == 0);
        CONTROLLA(ph_fin_imposta_densita_automatica(d, true, 6, NULL) == 0); /* si torna ad automatica: assestata */
        CONTROLLA(info(d, id).utile_w % 8 == 0 && info(d, id).utile_w > 0);
        (void)r5;
        PhFinSpec sq = { 400, 300, false, true };
        unsigned sp2 = ph_fin_apri(d, "specchio", "S", &sq, 0, NULL, NULL);
        CONTROLLA(info(d, sp2).rect.w == 400 && info(d, sp2).rect.h == 300 && info(d, sp2).utile_w == 0);
        ph_fin_libera(d);
    }
}

/* Il setter "solo verticale": finestra minimizzata, in registrazione, specchio. */
static void prova_solo_verticale_casi(void)
{
    PhFinDesktop *d = ph_fin_crea(1280, 800, 1.0, NULL);
    CONTROLLA(d != NULL);
    if (!d) return;
    CONTROLLA(ph_fin_imposta_qualita(d, 450, 160, 2560, 0, NULL) == 0);
    PhFinSpec sp = { 800, 500, false, false };
    unsigned mi = ph_fin_apri(d, "a", "a", &sp, 0, NULL, NULL);
    unsigned re = ph_fin_apri(d, "b", "b", &sp, 0, NULL, NULL);
    PhFinRichiesta rq[4];
    (void)ph_fin_avanza(d, 1000, rq, 4);
    /* minimizzata: la forma cambia subito, la richiesta parte 300 ms dopo il ripristino (non prima) */
    CONTROLLA(ph_fin_minimizza(d, mi, 2000, NULL) == 0);
    CONTROLLA(ph_fin_imposta_solo_verticale(d, mi, true, 2001, NULL) == 0);
    CONTROLLA(info(d, mi).rect.w == PH_FIN_TEL_W && info(d, mi).rect.h == PH_FIN_TEL_H && info(d, mi).minimizzata);
    CONTROLLA(ph_fin_avanza(d, 10000, rq, 4) == 0);
    CONTROLLA(ph_fin_ripristina(d, mi, 20000, NULL) == 0);
    CONTROLLA(ph_fin_avanza(d, 20299, rq, 4) == 0);
    size_t n = ph_fin_avanza(d, 20300, rq, 4);
    CONTROLLA(n == 1 && rq[0].id == mi && rq[0].w == 296 && rq[0].h == 440);
    CONTROLLA(ph_fin_avanza(d, 30000, rq, 4) == 0);
    /* in registrazione: la finestra prende la forma (e' una scelta della shell), ma al telefono nulla finche' registra */
    CONTROLLA(ph_fin_imposta_registrazione(d, re, true, 40000, NULL) == 0);
    CONTROLLA(ph_fin_imposta_solo_verticale(d, re, true, 40001, NULL) == 0);
    CONTROLLA(info(d, re).rect.w == PH_FIN_TEL_W && info(d, re).rect.h == PH_FIN_TEL_H && info(d, re).registrando);
    CONTROLLA(ph_fin_avanza(d, 50000, rq, 4) == 0);
    CONTROLLA(ph_fin_imposta_registrazione(d, re, false, 60000, NULL) == 0);
    CONTROLLA(ph_fin_avanza(d, 60299, rq, 4) == 0);
    n = ph_fin_avanza(d, 60300, rq, 4);
    CONTROLLA(n == 1 && rq[0].id == re && rq[0].w == 296 && rq[0].h == 440);
    /* specchio: rifiutato in entrambi i sensi, nulla cambia */
    PhFinSpec sq = { 400, 300, false, true };
    unsigned sc = ph_fin_apri(d, "specchio", "S", &sq, 70000, NULL, NULL);
    PhErrore e;
    PhFinRect r0 = info(d, sc).rect;
    CONTROLLA(ph_fin_imposta_solo_verticale(d, sc, true, 70001, &e) == -1 && e.testo[0]);
    CONTROLLA(!info(d, sc).solo_verticale && uguale(info(d, sc).rect, r0.x, r0.y, r0.w, r0.h));
    CONTROLLA(ph_fin_imposta_solo_verticale(d, sc, false, 70002, NULL) == -1);
    ph_fin_libera(d);
}


/* Assestamento: niente rientri nell'area, idempotente, origine su pixel fisico intero. */
static uint32_t lcg_a;
static int casuale_a(int n)
{
    lcg_a = lcg_a * 1664525u + 1013904223u;
    return (int)((lcg_a >> 8) % (uint32_t)n);
}

static void prova_assestamento_posizione(void)
{
    static const double sc[] = { 1.0, 1.25, 1.5, 2.0 };
    for (int k = 0; k < 4; k++) {
        double s = sc[k];
        PhFinDesktop *d = ph_fin_crea((int)(1280 * s), (int)(800 * s), s, NULL);
        CONTROLLA(d != NULL);
        if (!d) continue;
        unsigned a = apri(d, "web", 0), b = apri(d, "note", 0), c = apri(d, "set", 0);
        /* a meta' fuori a sinistra (a), a meta' fuori a destra (b): 96 px visibili bastano */
        CONTROLLA(ph_fin_gesto_inizia(d, a, PH_FIN_GESTO_SPOSTA, 300, 50, 1, NULL) == 0);
        CONTROLLA(ph_fin_gesto_muovi(d, 0, 50, 2, NULL) == 0);
        ph_fin_gesto_annulla(d, 3);
        CONTROLLA(ph_fin_gesto_inizia(d, b, PH_FIN_GESTO_SPOSTA, 200, 50, 4, NULL) == 0);
        CONTROLLA(ph_fin_gesto_muovi(d, 1250, 50, 5, NULL) == 0);
        ph_fin_gesto_annulla(d, 6);
        CONTROLLA(info(d, a).rect.x < 0 && info(d, b).rect.x + info(d, b).rect.w > 1280 + 100);
        /* ridimensionare a (a cavallo del bordo): x resta (a meno dell'allineamento, pochi px) */
        PhFinRect ra = info(d, a).rect;
        CONTROLLA(ph_fin_gesto_inizia(d, a, PH_FIN_GESTO_RIDIMENSIONA, ra.x + ra.w - 3, ra.y + ra.h - 3, 10, NULL) == 0);
        CONTROLLA(ph_fin_gesto_muovi(d, ra.x + ra.w + 40, ra.y + ra.h + 30, 11, NULL) == 0);
        CONTROLLA(ph_fin_gesto_finisci(d, ra.x + ra.w + 40, ra.y + ra.h + 30, 12, NULL, NULL) == 0);
        CONTROLLA(abs(info(d, a).rect.x - ra.x) <= 4 && info(d, a).rect.x < 0);
        if (k == 0) CONTROLLA(info(d, a).rect.x == ra.x && info(d, a).rect.y == ra.y);
        /* piede acceso/spento: le finestre a meta' fuori NON rientrano; dopo il primo allineamento le posizioni restano */
        CONTROLLA(ph_fin_imposta_pie(d, true, 100, NULL) == 0);
        PhFinRect pa = info(d, a).rect, pb = info(d, b).rect, pc = info(d, c).rect;
        CONTROLLA(pa.x < 0 && pb.x + pb.w > 1280 + 100 && abs(pa.x - ra.x) <= 4);
        CONTROLLA(ph_fin_imposta_pie(d, false, 101, NULL) == 0);
        CONTROLLA(ph_fin_imposta_pie(d, true, 102, NULL) == 0);
        CONTROLLA(info(d, a).rect.x == pa.x && info(d, a).rect.y == pa.y && info(d, a).rect.w == pa.w);
        CONTROLLA(info(d, b).rect.x == pb.x && info(d, b).rect.y == pb.y && info(d, c).rect.x == pc.x && info(d, c).rect.y == pc.y);
        /* origine del contenuto su un pixel fisico intero (queste scale lo permettono) */
        PhFinRect cc;
        CONTROLLA(ph_fin_contenuto(d, a, &cc, NULL) == 0);
        CONTROLLA(fabs(cc.x * s - floor(cc.x * s + 0.5)) < 1e-6 && fabs(cc.y * s - floor(cc.y * s + 0.5)) < 1e-6);
        ph_fin_libera(d);
    }

    /* idempotenza: assestare due volte di fila da' lo stesso rettangolo, a scale 1, 1,25, 1,5, 2 (e 1,37) e misure a caso */
    static const double sc2[] = { 1.0, 1.25, 1.5, 2.0, 1.37 };
    lcg_a = 12345;
    for (int k = 0; k < 5; k++)
        for (int giro = 0; giro < 6; giro++) {
            double s = sc2[k];
            PhFinDesktop *d = ph_fin_crea((int)(1200 * s), (int)(750 * s), s, NULL);
            CONTROLLA(d != NULL);
            if (!d) continue;
            unsigned ids[12];
            int n = 0;
            for (int i = 0; i < 10; i++) {
                char nome[8];
                snprintf(nome, sizeof nome, "w%d", i);
                PhFinSpec sp = { 200 + casuale_a(700), 150 + casuale_a(500), false, false };
                ids[n] = ph_fin_apri(d, nome, nome, &sp, 0, NULL, NULL);
                CONTROLLA(ids[n] != 0);
                n++;
                if (casuale_a(2)) { /* qualche spostamento */
                    PhFinRect r = info(d, ids[n - 1]).rect;
                    CONTROLLA(ph_fin_gesto_inizia(d, ids[n - 1], PH_FIN_GESTO_SPOSTA, r.x + 20, r.y + 10, 1, NULL) == 0);
                    CONTROLLA(ph_fin_gesto_muovi(d, casuale_a(1500) - 200, casuale_a(600), 2, NULL) == 0);
                    ph_fin_gesto_annulla(d, 3);
                }
            }
            CONTROLLA(ph_fin_imposta_pie(d, giro % 2 == 0, 10, NULL) == 0);
            PhFinRect prima[12];
            for (int i = 0; i < n; i++) prima[i] = info(d, ids[i]).rect;
            CONTROLLA(ph_fin_imposta_pie(d, giro % 2 == 0, 11, NULL) == 0);
            CONTROLLA(ph_fin_imposta_densita_automatica(d, true, 12, NULL) == 0);
            for (int i = 0; i < n; i++) {
                PhFinRect r = info(d, ids[i]).rect;
                int ok = r.x == prima[i].x && r.y == prima[i].y && r.w == prima[i].w && r.h == prima[i].h;
                if (!ok) fprintf(stderr, "non idempotente: scala %g (%d,%d,%d,%d) -> (%d,%d,%d,%d)\n", s, prima[i].x, prima[i].y, prima[i].w,
                                 prima[i].h, r.x, r.y, r.w, r.h);
                CONTROLLA(ok);
            }
            ph_fin_libera(d);
        }
    /* il caso citato: scala 1,5, larghezza 248 -> non 245 poi 240 */
    {
        PhFinDesktop *d = ph_fin_crea(1920, 1200, 1.5, NULL);
        CONTROLLA(d != NULL);
        if (d) {
            PhFinSpec sp = { 248, 300, false, false }; /* 248*1,5 = 372 -> 376 (multiplo di 8 piu' vicino) -> 251 logici */
            unsigned id = ph_fin_apri(d, "x", "x", &sp, 0, NULL, NULL);
            int w1 = info(d, id).rect.w;
            CONTROLLA(ph_fin_imposta_pie(d, true, 1, NULL) == 0 && ph_fin_imposta_pie(d, false, 2, NULL) == 0);
            CONTROLLA(info(d, id).rect.w == w1 && w1 * 3 / 2 % 8 <= 1);
            ph_fin_libera(d);
        }
    }
}


/* ---- Misure predefinite dallo schermo del telefono in punti (1 punto = 1 pixel logico, come DeX) ---- */
static PhFinDesktop *desk_logico(int lw, int lh, double s)
{
    PhFinDesktop *d = ph_fin_crea((int)(lw * s), (int)(lh * s), s, NULL);
    CONTROLLA(d != NULL);
    if (!d) exit(1);
    return d;
}

/* Apre con la misura predefinita e ritorna il contenuto (logico). */
static PhFinRect apri_pred(PhFinDesktop *d, const char *app, bool vert, bool spec, unsigned *idout)
{
    PhFinSpec sp = ph_fin_spec_predefinita(vert, spec);
    CONTROLLA(sp.w == 0 && sp.h == 0 && sp.solo_verticale == vert && sp.specchio == spec);
    unsigned id = ph_fin_apri(d, app, app, &sp, 0, NULL, NULL);
    CONTROLLA(id != 0);
    PhFinRect c = { 0, 0, 0, 0 };
    CONTROLLA(ph_fin_contenuto(d, id, &c, NULL) == 0);
    if (idout) *idout = id;
    return c;
}

static void prova_misure_telefono(void)
{
    static const double sc[] = { 1.0, 1.25, 1.5, 2.0 };
    PhErrore e;
    /* telefono non noto: il ripiego di prima */
    {
        PhFinDesktop *d = desk_logico(1920, 1200, 1.0);
        PhFinRect c = apri_pred(d, "a", true, false, NULL);
        CONTROLLA(c.w == PH_FIN_TEL_W && c.h == PH_FIN_TEL_H - PH_FIN_TITOLO_H);
        c = apri_pred(d, "b", false, true, NULL);
        CONTROLLA(c.w == PH_FIN_TEL_W && c.h == PH_FIN_TEL_H - PH_FIN_TITOLO_H);
        unsigned g = ph_fin_apri(d, "c", "c", NULL, 0, NULL, NULL);
        CONTROLLA(info(d, g).rect.w == PH_FIN_GENERICA_W && info(d, g).rect.h == PH_FIN_GENERICA_H);
        /* misure non valide: errore, nulla cambia */
        CONTROLLA(ph_fin_imposta_telefono(d, 0, 2340, 450, 0, &e) == -1 && e.testo[0]);
        CONTROLLA(ph_fin_imposta_telefono(d, 1080, 2340, 49, 0, NULL) == -1);
        CONTROLLA(ph_fin_imposta_telefono(d, 1080, 2340, 2001, 0, NULL) == -1);
        CONTROLLA(ph_fin_imposta_telefono(d, 30000, 2340, 450, 0, NULL) == -1);
        CONTROLLA(ph_fin_imposta_telefono(NULL, 1080, 2340, 450, 0, NULL) == -1);
        c = apri_pred(d, "d", true, false, NULL);
        CONTROLLA(c.w == PH_FIN_TEL_W); /* ancora il ripiego */
        /* dopo la chiamata le finestre gia' aperte non cambiano, le nuove si */
        PhFinRect r0 = info(d, ph_fin_cerca_app(d, "a")).rect;
        CONTROLLA(ph_fin_imposta_telefono(d, 1080, 2340, 450, 0, NULL) == 0);
        PhFinRect r1 = info(d, ph_fin_cerca_app(d, "a")).rect;
        CONTROLLA(uguale(r1, r0.x, r0.y, r0.w, r0.h));
        c = apri_pred(d, "e", true, false, NULL);
        CONTROLLA(c.w == 384 && c.h == 832);
        ph_fin_libera(d);
    }
    /* area grande, quattro scale: 1080x2340 a 450 dpi = 384x832 punti = 384x832 pixel logici = 384 s x 832 s fisici */
    for (int k = 0; k < 4; k++) {
        double s = sc[k];
        PhFinDesktop *d = desk_logico(1920, 1200, s);
        CONTROLLA(ph_fin_imposta_telefono(d, 1080, 2340, 450, 0, NULL) == 0);
        unsigned id;
        PhFinRect c = apri_pred(d, "v", true, false, &id); /* forma di telefono */
        CONTROLLA(c.w == 384 && c.h == 832 && info(d, id).solo_verticale);
        CONTROLLA(info(d, id).rect.w == 384 && info(d, id).rect.h == 832 + PH_FIN_TITOLO_H);
        CONTROLLA(info(d, id).utile_w == (int)(384 * s) && info(d, id).utile_h == (int)(832 * s));
        CONTROLLA(info(d, id).flusso_w == info(d, id).utile_w && info(d, id).flusso_h == info(d, id).utile_h);
        c = apri_pred(d, "s", false, true, &id); /* specchio: stessa proporzione */
        CONTROLLA(c.w == 384 && c.h == 832 && info(d, id).specchio);
        c = apri_pred(d, "g", false, false, &id); /* generica: stesse misure, ridimensionabile */
        CONTROLLA(c.w == 384 && c.h == 832 && !info(d, id).solo_verticale && info(d, id).puo_ridimensionare);
        ph_fin_libera(d);
    }
    /* area bassa (host logico 1280x722, area 674): ridotta in proporzione, ma la larghezza non scende sotto PH_FIN_MIN_W (288) */
    {
        PhFinDesktop *d = desk_logico(1280, 722, 1.0);
        CONTROLLA(ph_fin_imposta_telefono(d, 1080, 2340, 450, 0, NULL) == 0);
        unsigned id;
        PhFinRect c = apri_pred(d, "v", true, false, &id);
        CONTROLLA(c.w == 288 && c.h == 624 && info(d, id).rect.h == 674); /* k = 0,75: 288x624, piu' il titolo da 50 */
        ph_fin_libera(d);
    }
    /* area che obbliga a ridurre senza arrivare al minimo (area 800): k = (800-32-50)/832 = 0,863 -> 331x718, assestato 328x720 */
    {
        PhFinDesktop *d = desk_logico(1280, 848, 1.0);
        CONTROLLA(ph_fin_imposta_telefono(d, 1080, 2340, 450, 0, NULL) == 0);
        unsigned id;
        PhFinRect c = apri_pred(d, "v", true, false, &id);
        CONTROLLA(c.w == 328 && c.h == 720 && info(d, id).rect.h == 770);
        c = apri_pred(d, "g", false, false, &id);
        CONTROLLA(c.w == 328 && c.h == 720);
        ph_fin_libera(d);
    }
    /* telefono in orizzontale: la forma di telefono resta verticale, lo specchio conserva la proporzione dello schermo */
    {
        PhFinDesktop *d = desk_logico(1920, 1200, 1.0);
        CONTROLLA(ph_fin_imposta_telefono(d, 2340, 1080, 450, 0, NULL) == 0); /* 832x384 punti */
        unsigned id;
        PhFinRect c = apri_pred(d, "v", true, false, &id);
        CONTROLLA(c.w == 384 && c.h == 832);
        c = apri_pred(d, "g", false, false, &id);
        CONTROLLA(c.w == 384 && c.h == 832);
        c = apri_pred(d, "s", false, true, &id);
        CONTROLLA(c.w == 832 && c.h == 384);
        ph_fin_libera(d);
    }
    /* tablet 1600x2560 a 320 dpi = 800x1280 punti: sempre verticale, ridotto in proporzione (area 1152) a 672x1072 (k = (1152-32-50)/1280 = 0,836: 669x1070, assestato) */
    {
        PhFinDesktop *d = desk_logico(1920, 1200, 1.0);
        CONTROLLA(ph_fin_imposta_telefono(d, 1600, 2560, 320, 0, NULL) == 0);
        unsigned id;
        PhFinRect c = apri_pred(d, "v", true, false, &id);
        CONTROLLA(c.w == 672 && c.h == 1072);
        ph_fin_libera(d);
    }
    /* piede acceso: il contenuto resta 384x832 (la finestra e' piu' alta di 20 px) */
    {
        PhFinDesktop *d = desk_logico(1920, 1200, 1.0);
        CONTROLLA(ph_fin_imposta_pie(d, true, 0, NULL) == 0);
        CONTROLLA(ph_fin_imposta_telefono(d, 1080, 2340, 450, 0, NULL) == 0);
        unsigned id;
        PhFinRect c = apri_pred(d, "v", true, false, &id);
        CONTROLLA(c.w == 384 && c.h == 832 && info(d, id).rect.h == 832 + PH_FIN_CROM_H);
        ph_fin_libera(d);
    }
    /* il setter "solo verticale" usa la forma del telefono */
    {
        PhFinDesktop *d = desk_logico(1920, 1200, 1.0);
        CONTROLLA(ph_fin_imposta_telefono(d, 1080, 2340, 450, 0, NULL) == 0);
        PhFinSpec sp = { 900, 600, false, false };
        unsigned id = ph_fin_apri(d, "x", "x", &sp, 0, NULL, NULL);
        CONTROLLA(ph_fin_imposta_solo_verticale(d, id, true, 1, NULL) == 0);
        PhFinRect c;
        CONTROLLA(ph_fin_contenuto(d, id, &c, NULL) == 0 && c.w == 384 && c.h == 832);
        ph_fin_libera(d);
    }
    /* misure esplicite (mockup) restano come sono, anche con il telefono noto */
    {
        PhFinDesktop *d = desk_logico(1920, 1200, 1.0);
        CONTROLLA(ph_fin_imposta_telefono(d, 1080, 2340, 450, 0, NULL) == 0);
        PhFinSpec sp = ph_fin_spec_mockup("msg");
        unsigned id = ph_fin_apri(d, "msg", "m", &sp, 0, NULL, NULL);
        CONTROLLA(info(d, id).rect.w == 296 && info(d, id).rect.h == 470);
        PhFinSpec bad = { 0, 100, false, false };
        CONTROLLA(ph_fin_apri(d, "bad", "b", &bad, 0, NULL, &e) == 0 && e.testo[0]);
        ph_fin_libera(d);
    }
}


/* ---- Riproporzione: un'app non vede mai meno punti di larghezza di quelli del telefono ---- */
static PhFinInfo una_stretta(double s, int cw, int ch, bool noto, bool automatica, int lh)
{
    PhFinDesktop *d = ph_fin_crea((int)(1920 * s), (int)((lh + 48) * s), s, NULL);
    CONTROLLA(d != NULL);
    PhFinInfo in;
    memset(&in, 0, sizeof in);
    if (!d) return in;
    CONTROLLA(ph_fin_imposta_qualita(d, 450, 160, 2560, 0, NULL) == 0);
    if (noto) CONTROLLA(ph_fin_imposta_telefono(d, 1080, 2340, 450, 0, NULL) == 0);
    CONTROLLA(ph_fin_imposta_densita_automatica(d, automatica, 0, NULL) == 0);
    PhFinSpec sp = { cw, ch + PH_FIN_TITOLO_H, false, false };
    unsigned id = ph_fin_apri(d, "x", "x", &sp, 0, NULL, NULL);
    CONTROLLA(id != 0);
    in = info(d, id);
    ph_fin_libera(d);
    return in;
}

static void prova_riproporzione(void)
{
    PhFinInfo in;
    /* Wt = 1080*160/450 = 384 punti. A scala 1: 384 -> 160 dpi (f = 1); 288 (= PH_FIN_MIN_W, il minimo) -> 120 dpi, il telefono vede 384 punti */
    in = una_stretta(1.0, 384, 600, true, true, 1100);
    CONTROLLA(in.fattore == 1.0 && in.densita == 160 && in.punti_w == 384 && in.punti_h == 600);
    in = una_stretta(1.0, 288, 600, true, true, 1100);
    CONTROLLA(in.fattore == 0.75 && in.densita == 120 && in.flusso_w == 288 && in.flusso_h == 600);
    CONTROLLA(in.punti_w == 384 && in.punti_h == 800); /* l'altezza in proporzione */
    in = una_stretta(1.0, 240, 600, true, true, 1100); /* sotto il minimo: il contenuto sale a 288 (stesso risultato) */
    CONTROLLA(in.fattore == 0.75 && in.densita == 120 && in.punti_w == 384 && in.punti_h == 800);
    in = una_stretta(1.0, 392, 600, true, true, 1100); /* piu' larga del telefono: nulla cambia */
    CONTROLLA(in.fattore == 1.0 && in.densita == 160 && in.punti_w == 392);
    /* scale 1,25 / 1,5 / 2: 288 punti logici = 360 / 432 / 576 pixel fisici; densita' = 160*scala*0,75 */
    in = una_stretta(1.25, 288, 600, true, true, 1100);
    CONTROLLA(in.densita == 150 && in.flusso_w == 360 && in.punti_w == 384);
    in = una_stretta(1.5, 288, 600, true, true, 1100);
    CONTROLLA(in.densita == 180 && in.flusso_w == 432 && in.punti_w == 384);
    in = una_stretta(2.0, 288, 600, true, true, 1100);
    CONTROLLA(in.densita == 240 && in.flusso_w == 576 && in.punti_w == 384);
    /* a scala 1,25 con 384: f = 1, 200 dpi (scalino di 20) */
    in = una_stretta(1.25, 384, 600, true, true, 1100);
    CONTROLLA(in.fattore == 1.0 && in.densita == 200 && in.punti_w == 384);
    /* telefono non noto: f = 1 anche per una finestra stretta; densita' fissa: invariata; densita' fissa non riproporziona */
    in = una_stretta(1.0, 288, 600, false, true, 1100);
    CONTROLLA(in.fattore == 1.0 && in.densita == 160 && in.punti_w == 288);
    in = una_stretta(1.0, 288, 600, true, false, 1100);
    CONTROLLA(in.fattore == 1.0 && in.densita == 450);
    /* oltre il tetto (scala 2, 288x1700 logici = 576x3400 fisici): i punti visti restano ~Wt */
    in = una_stretta(2.0, 288, 1700, true, true, 1850);
    CONTROLLA(in.flusso_h == 2560 && in.densita >= 178 && in.densita <= 184);
    CONTROLLA(in.punti_w >= 384 && in.punti_w <= 388);
    /* continuita' attorno a Wt: da 288 a 520 la densita' non diminuisce, i punti visti sono sempre >= ~Wt e = w da Wt in su */
    {
        int prec = 0;
        for (int w = 288; w <= 520; w += 8) {
            in = una_stretta(1.0, w, 600, true, true, 1100);
            CONTROLLA(in.densita >= prec && in.punti_w >= 384); /* mai sotto Wt, troncati come Android */
            if (w >= 384) CONTROLLA(in.punti_w == w && in.fattore == 1.0);
            prec = in.densita;
        }
        in = una_stretta(1.0, 376, 600, true, true, 1100); /* appena sotto: 156 dpi (per difetto), 385 punti (mai sotto 384) */
        CONTROLLA(in.densita == 156 && in.punti_w == 385);
    }
    /* specchio: nessuna densita' ne' riproporzione */
    {
        PhFinDesktop *d = ph_fin_crea(1920, 1200, 1.0, NULL);
        CONTROLLA(d != NULL);
        if (d) {
            CONTROLLA(ph_fin_imposta_telefono(d, 1080, 2340, 450, 0, NULL) == 0);
            PhFinSpec sq = { 288, 600, false, true };
            unsigned id = ph_fin_apri(d, "s", "s", &sq, 0, NULL, NULL);
            in = info(d, id);
            CONTROLLA(in.densita == 0 && in.fattore == 1.0 && in.punti_w == 0);
            ph_fin_libera(d);
        }
    }
    /* telefono in orizzontale: Wt e' comunque il lato corto (384) */
    {
        PhFinDesktop *d = ph_fin_crea(1920, 1200, 1.0, NULL);
        CONTROLLA(d != NULL);
        if (d) {
            CONTROLLA(ph_fin_imposta_qualita(d, 450, 160, 2560, 0, NULL) == 0);
            CONTROLLA(ph_fin_imposta_telefono(d, 2340, 1080, 450, 0, NULL) == 0);
            PhFinSpec sp = { 288, 632, false, false };
            unsigned id = ph_fin_apri(d, "x", "x", &sp, 0, NULL, NULL);
            CONTROLLA(info(d, id).densita == 120 && info(d, id).punti_w == 384);
            ph_fin_libera(d);
        }
    }
    /* forma di telefono ridotta dall'area (area bassa, contenuto 288 largo, il minimo): densita' 120 e 384 punti visti */
    {
        PhFinDesktop *d = ph_fin_crea(1280, 722, 1.0, NULL);
        CONTROLLA(d != NULL);
        if (d) {
            CONTROLLA(ph_fin_imposta_qualita(d, 450, 160, 2560, 0, NULL) == 0);
            CONTROLLA(ph_fin_imposta_telefono(d, 1080, 2340, 450, 0, NULL) == 0);
            PhFinSpec sp = ph_fin_spec_predefinita(true, false);
            unsigned id = ph_fin_apri(d, "v", "v", &sp, 0, NULL, NULL);
            CONTROLLA(info(d, id).rect.w == 288 && info(d, id).densita == 120 && info(d, id).punti_w == 384);
            ph_fin_libera(d);
        }
    }
    /* ridimensionata stretta dall'utente: durante il gesto nessuna richiesta, poi UNA sola con la nuova densita' */
    {
        PhFinDesktop *d = ph_fin_crea(1920, 1200, 1.0, NULL);
        CONTROLLA(d != NULL);
        if (d) {
            CONTROLLA(ph_fin_imposta_qualita(d, 450, 160, 2560, 0, NULL) == 0);
            CONTROLLA(ph_fin_imposta_telefono(d, 1080, 2340, 450, 0, NULL) == 0);
            PhFinSpec sp = { 400, 650, false, false }; /* contenuto 400x600: f = 1, 160 dpi */
            unsigned id = ph_fin_apri(d, "x", "x", &sp, 0, NULL, NULL);
            CONTROLLA(info(d, id).densita == 160);
            PhFinRect r = info(d, id).rect;
            PhFinRichiesta rq[4];
            CONTROLLA(ph_fin_gesto_inizia(d, id, PH_FIN_GESTO_RIDIMENSIONA, r.x + r.w - 3, r.y + r.h - 3, 1000, NULL) == 0);
            CONTROLLA(ph_fin_gesto_muovi(d, r.x + r.w - 3 - 112, r.y + r.h - 3, 1100, NULL) == 0);
            CONTROLLA(ph_fin_avanza(d, 1500, rq, 4) == 0);
            CONTROLLA(ph_fin_gesto_finisci(d, r.x + r.w - 3 - 112, r.y + r.h - 3, 2000, NULL, NULL) == 0);
            CONTROLLA(info(d, id).rect.w == 288 && info(d, id).densita == 120);
            CONTROLLA(ph_fin_avanza(d, 2299, rq, 4) == 0);
            CONTROLLA(ph_fin_avanza(d, 2300, rq, 4) == 1 && rq[0].id == id && rq[0].densita == 120 && rq[0].w == 288 && rq[0].h == 600);
            CONTROLLA(ph_fin_avanza(d, 9000, rq, 4) == 0);
            ph_fin_libera(d);
        }
    }
}


/* Mai sotto Wt punti (troncati come Android), a scale dove 160*scala non e' multiplo di 20 e con un telefono da 360 punti. */
static void prova_punti_minimi(void)
{
    static const double sc[] = { 1.0, 1.1, 1.2, 1.25, 1.37, 1.5, 2.0 };
    for (int tel = 0; tel < 2; tel++) {
        int wtp = tel ? 360 : 384;
        for (int k = 0; k < 7; k++) {
            double s = sc[k];
            for (int w = wtp - 2; w <= wtp + 12; w++) {
                PhFinDesktop *d = ph_fin_crea((int)(1920 * s), (int)(1200 * s), s, NULL);
                CONTROLLA(d != NULL);
                if (!d) continue;
                CONTROLLA(ph_fin_imposta_qualita(d, tel ? 480 : 450, 160, 2560, 0, NULL) == 0);
                CONTROLLA(ph_fin_imposta_telefono(d, 1080, 2340, tel ? 480 : 450, 0, NULL) == 0);
                PhFinSpec sp = { w, 632, false, false };
                unsigned id = ph_fin_apri(d, "x", "x", &sp, 0, NULL, NULL);
                PhFinInfo in = info(d, id);
                PhFinRect c;
                CONTROLLA(ph_fin_contenuto(d, id, &c, NULL) == 0);
                int ok = in.punti_w >= wtp && in.densita >= 50 && in.densita <= (tel ? 480 : 450);
                if (!ok) fprintf(stderr, "scala %g Wt %d w richiesta %d contenuto %d: densita %d punti %d\n", s, wtp, w, c.w, in.densita, in.punti_w);
                CONTROLLA(ok);
                if (c.w >= wtp + 8) CONTROLLA(in.fattore > 0.97); /* sopra Wt (a meno dell'arrotondamento a 8 pixel fisici) f e' ~1 */
                ph_fin_libera(d);
            }
        }
    }
    /* il caso citato: scala 1,1, contenuto 384 -> la densita' e' 176 (160*1,1), non lo scalino 180: 384*1,1 = 422,4 -> 424 px, 385 punti */
    {
        PhFinDesktop *d = ph_fin_crea((int)(1920 * 1.1), (int)(1200 * 1.1), 1.1, NULL);
        CONTROLLA(d != NULL);
        if (d) {
            CONTROLLA(ph_fin_imposta_qualita(d, 450, 160, 2560, 0, NULL) == 0);
            CONTROLLA(ph_fin_imposta_telefono(d, 1080, 2340, 450, 0, NULL) == 0);
            PhFinSpec sp = { 384, 632, false, false };
            unsigned id = ph_fin_apri(d, "x", "x", &sp, 0, NULL, NULL);
            PhFinInfo in = info(d, id);
            CONTROLLA(in.densita <= 177 && in.punti_w >= 384);
            ph_fin_libera(d);
        }
    }
}


/* Lo specchio ha SEMPRE forma fissa (anche creato senza il segno "solo verticale"). */
static void prova_specchio_fisso(void)
{
    PhFinDesktop *d = ph_fin_crea(1920, 1200, 1.0, NULL);
    CONTROLLA(d != NULL);
    if (!d) return;
    CONTROLLA(ph_fin_imposta_qualita(d, 450, 160, 2560, 0, NULL) == 0);
    CONTROLLA(ph_fin_imposta_telefono(d, 1080, 2340, 450, 0, NULL) == 0);
    PhFinSpec sp = ph_fin_spec_predefinita(false, true); /* niente solo_verticale */
    CONTROLLA(!sp.solo_verticale && sp.specchio);
    unsigned s = ph_fin_apri(d, "specchio", "S", &sp, 0, NULL, NULL);
    CONTROLLA(s != 0);
    PhFinInfo in = info(d, s);
    PhErrore e;
    CONTROLLA(!in.solo_verticale && in.specchio && !in.puo_ridimensionare && !in.puo_ingrandire && !in.puo_agganciare);
    PhFinRect r0 = in.rect;
    CONTROLLA(r0.w == 384 && r0.h == 832 + PH_FIN_TITOLO_H);
    /* ridimensionamento, ingrandimento, rotazione rifiutati: nulla cambia */
    CONTROLLA(ph_fin_gesto_inizia(d, s, PH_FIN_GESTO_RIDIMENSIONA, r0.x + r0.w - 3, r0.y + r0.h - 3, 1, &e) == -1 && e.testo[0]);
    CONTROLLA(ph_fin_gesto_finestra(d) == 0);
    CONTROLLA(ph_fin_massimizza_ripristina(d, s, 2, &e) == -1 && e.testo[0]);
    CONTROLLA(ph_fin_ruota(d, s, 3, &e) == -1 && e.testo[0]);
    CONTROLLA(info(d, s).stato == PH_FIN_NORMALE && uguale(info(d, s).rect, r0.x, r0.y, r0.w, r0.h));
    /* nessun aggancio ai bordi, ma si sposta */
    PhFinAggancio ag = PH_FIN_AGG_A_SINISTRA;
    CONTROLLA(ph_fin_gesto_inizia(d, s, PH_FIN_GESTO_SPOSTA, r0.x + 50, r0.y + 10, 4, NULL) == 0);
    CONTROLLA(ph_fin_gesto_muovi(d, 3, 5, 5, NULL) == 0);
    CONTROLLA(ph_fin_gesto_anteprima(d, NULL) == PH_FIN_AGG_NESSUNO);
    CONTROLLA(ph_fin_gesto_finisci(d, 3, 5, 6, &ag, NULL) == 0 && ag == PH_FIN_AGG_NESSUNO);
    CONTROLLA(info(d, s).stato == PH_FIN_NORMALE && info(d, s).rect.w == 384 && info(d, s).rect.h == r0.h);
    CONTROLLA(info(d, s).rect.x != r0.x || info(d, s).rect.y != r0.y); /* spostata */
    /* minimizzare, ripristinare, chiudere */
    CONTROLLA(ph_fin_minimizza(d, s, 7, NULL) == 0 && ph_fin_ripristina(d, s, 8, NULL) == 0);
    /* l'area cambia: la forma si riduce in proporzione se non sta, e torna quando c'e' posto */
    CONTROLLA(ph_fin_imposta_host(d, 1280, 600 + 48, 1.0, 9, NULL) == 0); /* area bassa: 600 */
    PhFinRect rb = info(d, s).rect;
    CONTROLLA(rb.h <= 600 && rb.w < 384 && abs(rb.w * (832 + 50) - 384 * rb.h) < 2 * (832 + 50));
    CONTROLLA(ph_fin_imposta_host(d, 1920, 1200, 1.0, 10, NULL) == 0);
    CONTROLLA(info(d, s).rect.w == 384 && info(d, s).rect.h == r0.h);
    /* il telefono ruota a specchio aperto: la finestra non cambia (immagine con bande) */
    CONTROLLA(ph_fin_imposta_telefono(d, 2340, 1080, 450, 11, NULL) == 0);
    CONTROLLA(info(d, s).rect.w == 384 && info(d, s).rect.h == r0.h);
    CONTROLLA(ph_fin_chiudi(d, s, 12, NULL) == 0);
    ph_fin_libera(d);
}


/* Una finestra puo' sporgere in basso (resta visibile la barra del titolo): "metterla da parte". */
static void prova_sporge_in_basso(void)
{
    static const double sc[] = { 1.0, 1.25 };
    for (int k = 0; k < 2; k++) {
        double s = sc[k];
        PhFinDesktop *d = ph_fin_crea((int)(1280 * s), (int)((700 + 48) * s), s, NULL); /* area 1280x700 */
        CONTROLLA(d != NULL);
        if (!d) continue;
        CONTROLLA(ph_fin_imposta_qualita(d, 450, 160, 2560, 0, NULL) == 0);
        CONTROLLA(ph_fin_imposta_telefono(d, 1080, 2340, 450, 0, NULL) == 0);
        PhFinSpec sg = { 500, 332, false, false };
        PhFinSpec sv = ph_fin_spec_predefinita(true, false), sm = ph_fin_spec_predefinita(false, true);
        unsigned g = ph_fin_apri(d, "g", "g", &sg, 0, NULL, NULL);
        unsigned v = ph_fin_apri(d, "v", "v", &sv, 0, NULL, NULL);
        unsigned m = ph_fin_apri(d, "m", "m", &sm, 0, NULL, NULL);
        CONTROLLA(g && v && m);
        /* (4) apertura: tutte dentro */
        unsigned idv[3] = { g, v, m };
        for (int i = 0; i < 3; i++) {
            PhFinRect r = info(d, idv[i]).rect;
            CONTROLLA(r.x >= 0 && r.y >= 0 && r.x + r.w <= 1280 && r.y + r.h <= 700);
        }
        for (int i = 0; i < 3; i++) {
            PhFinInfo prima = info(d, idv[i]);
            /* trascinare verso il basso: fino a H - altezza del titolo; in alto y >= 0 */
            PhFinRect r = prima.rect;
            CONTROLLA(ph_fin_gesto_inizia(d, idv[i], PH_FIN_GESTO_SPOSTA, r.x + 40, r.y + 10, 1, NULL) == 0);
            CONTROLLA(ph_fin_gesto_muovi(d, r.x + 40, 5000, 2, NULL) == 0);
            CONTROLLA(info(d, idv[i]).rect.y == 700 - PH_FIN_TITOLO_H);
            CONTROLLA(ph_fin_gesto_muovi(d, r.x + 40, -5000, 3, NULL) == 0);
            CONTROLLA(info(d, idv[i]).rect.y == 0);
            CONTROLLA(ph_fin_gesto_muovi(d, r.x + 40, 690, 4, NULL) == 0);
            /* (1) a fine movimento nessun rientro ne' rimpicciolimento */
            PhFinAggancio ag;
            CONTROLLA(ph_fin_gesto_finisci(d, r.x + 40, 690, 5, &ag, NULL) == 0 && ag == PH_FIN_AGG_NESSUNO);
            PhFinInfo dopo = info(d, idv[i]);
            CONTROLLA(dopo.rect.y > 700 - dopo.rect.h && dopo.rect.y <= 700 - PH_FIN_TITOLO_H); /* sporge */
            CONTROLLA(dopo.rect.w == prima.rect.w && dopo.rect.h == prima.rect.h);
            /* (5) flusso e densita' non cambiano perche' la finestra sporge */
            CONTROLLA(dopo.flusso_w == prima.flusso_w && dopo.flusso_h == prima.flusso_h && dopo.densita == prima.densita &&
                      dopo.utile_w == prima.utile_w && dopo.utile_h == prima.utile_h);
            PhFinRichiesta rq[4];
            size_t nr = ph_fin_avanza(d, 100000 + 1000 * (uint64_t)i, rq, 4);
            for (size_t q = 0; q < nr; q++) CONTROLLA(rq[q].id != idv[i]);
        }
        /* sporge di ~100 px: l'assestamento (piede acceso/spento) non la accorcia a H - y */
        {
            PhFinRect rr = info(d, g).rect;
            CONTROLLA(ph_fin_gesto_inizia(d, g, PH_FIN_GESTO_SPOSTA, rr.x + 60, rr.y + 10, 150000, NULL) == 0);
            CONTROLLA(ph_fin_gesto_muovi(d, rr.x + 60, 700 - rr.h + 100 + 10, 150001, NULL) == 0);
            ph_fin_gesto_annulla(d, 150002);
            PhFinRect r1 = info(d, g).rect;
            CONTROLLA(r1.y + r1.h > 700 + 90);
            CONTROLLA(ph_fin_imposta_pie(d, true, 150010, NULL) == 0);
            CONTROLLA(ph_fin_imposta_pie(d, false, 150011, NULL) == 0);
            PhFinRect r2 = info(d, g).rect;
            CONTROLLA(abs(r2.y - r1.y) <= 4 && abs(r2.h - r1.h) <= 8 && (k != 0 || r2.y == r1.y)); /* l'allineamento a pixel fisici sposta di pochi px solo se la scala non e' 1 */
        }
        /* piede acceso/spento (assestamento di tutte): le finestre che sporgono restano dove sono e della stessa altezza (+-8) */
        PhFinRect pr[3];
        for (int i = 0; i < 3; i++) pr[i] = info(d, idv[i]).rect;
        CONTROLLA(ph_fin_imposta_pie(d, true, 200000, NULL) == 0);
        CONTROLLA(ph_fin_imposta_pie(d, false, 200001, NULL) == 0);
        for (int i = 0; i < 3; i++) {
            PhFinRect r = info(d, idv[i]).rect;
            CONTROLLA(r.y == pr[i].y && abs(r.h - pr[i].h) <= 8 && r.y + r.h > 700);
        }
        /* (2) ridimensionare una finestra normale che sporge (programmaticamente dal punto in basso): nessun salto */
        PhFinRect rg = info(d, g).rect;
        CONTROLLA(ph_fin_gesto_inizia(d, g, PH_FIN_GESTO_RIDIMENSIONA, rg.x + rg.w - 3, 699, 300000, NULL) == 0);
        CONTROLLA(ph_fin_gesto_muovi(d, rg.x + rg.w - 3 + 30, 699, 300001, NULL) == 0);
        CONTROLLA(ph_fin_gesto_finisci(d, rg.x + rg.w - 3 + 30, 699, 300002, NULL, NULL) == 0);
        PhFinRect rg2 = info(d, g).rect;
        CONTROLLA(rg2.y == rg.y && abs(rg2.h - rg.h) <= 8 && rg2.w > rg.w);
        /* (6) punti fuori dall'area: la parte che sporge sotto il bordo non e' di nessuna finestra */
        PhFinColpo col;
        ph_fin_trova(d, rg2.x + 20, 700 + 5, &col);
        CONTROLLA(col.id == 0 && col.zona == PH_FIN_ZONA_FUORI);
        ph_fin_trova(d, rg2.x + 60, rg2.y + 10, &col);
        CONTROLLA(col.id == g && col.zona == PH_FIN_ZONA_TITOLO);
        ph_fin_trova(d, -3, 10, &col);
        CONTROLLA(col.id == 0);
        /* (3) l'area si riduce: riportate solo quanto basta (titolo visibile), non di piu' */
        CONTROLLA(ph_fin_imposta_host(d, (int)(1280 * s), (int)((400 + 48) * s), s, 400000, NULL) == 0); /* area 1280x400 */
        for (int i = 0; i < 3; i++) {
            PhFinInfo in = info(d, idv[i]);
            CONTROLLA(in.rect.y == 400 - PH_FIN_TITOLO_H || in.rect.y < 400 - PH_FIN_TITOLO_H);
            if (pr[i].y > 400 - PH_FIN_TITOLO_H) CONTROLLA(in.rect.y == 400 - PH_FIN_TITOLO_H); /* non di piu' */
        }
        /* una finestra piu' in alto non si muove */
        CONTROLLA(ph_fin_gesto_inizia(d, g, PH_FIN_GESTO_SPOSTA, rg2.x + 40, 300, 500000, NULL) == 0);
        CONTROLLA(ph_fin_gesto_muovi(d, rg2.x + 40, 200, 500001, NULL) == 0);
        ph_fin_gesto_annulla(d, 500002);
        int y0 = info(d, g).rect.y;
        CONTROLLA(ph_fin_imposta_host(d, (int)(1280 * s), (int)((380 + 48) * s), s, 600000, NULL) == 0);
        CONTROLLA(info(d, g).rect.y == (y0 < 380 - PH_FIN_TITOLO_H ? y0 : 380 - PH_FIN_TITOLO_H));
        /* (4) cascata: nuove finestre tutte dentro anche con quelle vecchie che sporgono */
        char nome[8];
        for (int i = 0; i < 12; i++) {
            snprintf(nome, sizeof nome, "n%d", i);
            unsigned nid = ph_fin_apri(d, nome, nome, &sg, 700000, NULL, NULL);
            PhFinRect r = info(d, nid).rect;
            CONTROLLA(r.x >= 0 && r.y >= 0 && r.x + r.w <= 1280 && r.y + r.h <= 380);
        }
        ph_fin_libera(d);
    }
}


/* Cambio d'area: chi era dentro resta dentro; chi sporgeva si sposta solo quanto basta. Rotazione e setter: tutta dentro. */
static void metti_in(PhFinDesktop *d, unsigned id, int x, int y)
{
    PhFinRect r = info(d, id).rect;
    CONTROLLA(ph_fin_gesto_inizia(d, id, PH_FIN_GESTO_SPOSTA, r.x + 10, r.y + 10, 1, NULL) == 0);
    CONTROLLA(ph_fin_gesto_muovi(d, x + 10, y + 10, 2, NULL) == 0);
    ph_fin_gesto_annulla(d, 3);
}

static void prova_cambio_area(void)
{
    PhFinDesktop *d = nuovo(1280, 748, 1.0); /* area 1280x700, densita' fissa: nessun assestamento */
    PhFinSpec sp = { 300, 232, false, false };
    unsigned a = ph_fin_apri(d, "a", "a", &sp, 0, NULL, NULL), b = ph_fin_apri(d, "b", "b", &sp, 0, NULL, NULL),
             c = ph_fin_apri(d, "c", "c", &sp, 0, NULL, NULL), e = ph_fin_apri(d, "e", "e", &sp, 0, NULL, NULL);
    metti_in(d, a, 900, 450);  /* tutta dentro, vicino all'angolo in basso a destra */
    metti_in(d, b, 1200, 100); /* sporge a destra */
    metti_in(d, c, 400, 650);  /* sporge in basso */
    metti_in(d, e, 100, 100);  /* dentro, in alto a sinistra */
    PhFinRect re0 = info(d, e).rect;
    CONTROLLA(info(d, a).rect.x + info(d, a).rect.w <= 1280 && info(d, a).rect.y + info(d, a).rect.h <= 700);
    CONTROLLA(info(d, b).rect.x + info(d, b).rect.w > 1280 && info(d, c).rect.y + info(d, c).rect.h > 700);
    /* area 800x500: a (era dentro) torna tutta dentro; b e c solo quanto basta */
    CONTROLLA(ph_fin_imposta_host(d, 800, 548, 1.0, 10, NULL) == 0);
    PhFinRect ra = info(d, a).rect, rb = info(d, b).rect, rc = info(d, c).rect, re = info(d, e).rect;
    CONTROLLA(ra.x >= 0 && ra.y >= 0 && ra.x + ra.w <= 800 && ra.y + ra.h <= 500 && ra.w == 300 && ra.h == 232); /* larghezza e altezza */
    CONTROLLA(rb.x == 800 - PH_FIN_VISIBILE_MIN && rb.y == 100); /* in larghezza: solo quanto basta, y non si tocca */
    CONTROLLA(rc.y == 500 - PH_FIN_TITOLO_H && rc.x == 400);     /* in altezza: solo quanto basta, x non si tocca */
    CONTROLLA(re.x == re0.x && re.y == re0.y);                    /* gia' dentro e ancora dentro: ferma */
    /* il pulsante Chiudi di a e' dentro l'area */
    CONTROLLA(ra.x + ra.w - 4 <= 800 && ra.y + PH_FIN_TITOLO_H <= 500);
    /* area ancora piu' piccola (400x300): a non ci sta nemmeno ridotta alla sua misura: ridotta all'area, titolo in alto a sinistra */
    CONTROLLA(ph_fin_imposta_host(d, 400, 348, 1.0, 20, NULL) == 0);
    ra = info(d, a).rect;
    CONTROLLA(ra.w <= 400 && ra.h <= 300 && ra.x >= 0 && ra.y >= 0 && ra.x + ra.w <= 400 && ra.y + ra.h <= 300);
    ph_fin_libera(d);

    /* area che si allarga e si restringe di nuovo: una finestra dentro resta dentro in larghezza ed altezza separatamente */
    d = nuovo(1280, 748, 1.0);
    unsigned w1 = ph_fin_apri(d, "w1", "w1", &sp, 0, NULL, NULL), h1 = ph_fin_apri(d, "h1", "h1", &sp, 0, NULL, NULL);
    metti_in(d, w1, 950, 100); /* dentro: a destra */
    metti_in(d, h1, 100, 450); /* dentro: in basso */
    CONTROLLA(ph_fin_imposta_host(d, 900, 748, 1.0, 1, NULL) == 0); /* solo la larghezza */
    CONTROLLA(info(d, w1).rect.x + info(d, w1).rect.w <= 900 && info(d, h1).rect.x == 100);
    CONTROLLA(ph_fin_imposta_host(d, 900, 500, 1.0, 2, NULL) == 0); /* solo l'altezza (area 452) */
    CONTROLLA(info(d, h1).rect.y + info(d, h1).rect.h <= 452 && info(d, h1).rect.x == 100);
    ph_fin_libera(d);

    /* rotazione: una finestra in basso ruotata resta tutta dentro (non e' un movimento dell'utente) */
    d = nuovo(980, 584, 1.0); /* area 980x536 */
    PhFinSpec sq = { 400, 290, false, false };
    unsigned n = ph_fin_apri(d, "n", "n", &sq, 0, NULL, NULL);
    metti_in(d, n, 100, 240); /* 400x290 -> bordo inferiore 530 */
    CONTROLLA(ph_fin_ruota(d, n, 4, NULL) == 0); /* 288x470 */
    PhFinRect rr = info(d, n).rect;
    CONTROLLA(rr.w == 288 && rr.h == 470 && rr.y >= 0 && rr.y + rr.h <= 536 && rr.x >= 0 && rr.x + rr.w <= 980);
    /* setter "solo verticale": tutta dentro, anche se la forma e' piu' alta dell'area (ridotta in proporzione) */
    CONTROLLA(ph_fin_imposta_telefono(d, 1080, 2340, 450, 0, NULL) == 0);
    PhFinSpec sw = { 500, 332, false, false };
    unsigned v = ph_fin_apri(d, "v", "v", &sw, 0, NULL, NULL);
    metti_in(d, v, 600, 300);
    CONTROLLA(ph_fin_imposta_solo_verticale(d, v, true, 5, NULL) == 0);
    rr = info(d, v).rect;
    CONTROLLA(rr.x >= 0 && rr.y >= 0 && rr.x + rr.w <= 980 && rr.y + rr.h <= 536);
    ph_fin_libera(d);
}


/* La misura voluta: una finestra ridotta da un'area stretta torna com'era quando l'area ricresce. */
static void prova_misura_voluta(void)
{
    /* densita' fissa: nessun assestamento, numeri esatti */
    PhFinDesktop *d = nuovo(1280, 748, 1.0); /* area 1280x700 */
    PhFinSpec sp = { 540, 340, false, false };
    unsigned a = ph_fin_apri(d, "a", "a", &sp, 0, NULL, NULL), b = ph_fin_apri(d, "b", "b", &sp, 0, NULL, NULL),
             c = ph_fin_apri(d, "c", "c", &sp, 0, NULL, NULL);
    CONTROLLA(info(d, a).rect.w == 540 && info(d, a).rect.h == 340);
    /* area stretta 400x300: ridotte all'area */
    CONTROLLA(ph_fin_imposta_host(d, 400, 348, 1.0, 1, NULL) == 0);
    CONTROLLA(info(d, a).rect.w == 400 && info(d, a).rect.h == 300);
    /* ... e di nuovo larga: misura di prima esatta */
    CONTROLLA(ph_fin_imposta_host(d, 1280, 748, 1.0, 2, NULL) == 0);
    CONTROLLA(info(d, a).rect.w == 540 && info(d, a).rect.h == 340);
    CONTROLLA(info(d, b).rect.w == 540 && info(d, c).rect.h == 340);
    /* piu' passaggi (stretta, media, larga): sempre ridotta solo quanto serve */
    CONTROLLA(ph_fin_imposta_host(d, 500, 348, 1.0, 3, NULL) == 0);
    CONTROLLA(info(d, a).rect.w == 500 && info(d, a).rect.h == 300);
    CONTROLLA(ph_fin_imposta_host(d, 520, 548, 1.0, 4, NULL) == 0);
    CONTROLLA(info(d, a).rect.w == 520 && info(d, a).rect.h == 340);
    CONTROLLA(ph_fin_imposta_host(d, 1280, 748, 1.0, 5, NULL) == 0);
    CONTROLLA(info(d, a).rect.w == 540 && info(d, a).rect.h == 340);
    /* il ridimensionamento dell'utente mentre e' ridotta diventa la voluta */
    CONTROLLA(ph_fin_imposta_host(d, 400, 348, 1.0, 10, NULL) == 0);
    PhFinRect r = info(d, a).rect;
    CONTROLLA(ph_fin_gesto_inizia(d, a, PH_FIN_GESTO_RIDIMENSIONA, r.x + r.w - 3, r.y + r.h - 3, 11, NULL) == 0);
    CONTROLLA(ph_fin_gesto_muovi(d, r.x + r.w - 3 - 100, r.y + r.h - 3 - 60, 12, NULL) == 0);
    CONTROLLA(ph_fin_gesto_finisci(d, r.x + r.w - 3 - 100, r.y + r.h - 3 - 60, 13, NULL, NULL) == 0);
    int vw = info(d, a).rect.w, vh = info(d, a).rect.h;
    CONTROLLA(vw == 300 && vh == 240);
    CONTROLLA(ph_fin_imposta_host(d, 1280, 748, 1.0, 14, NULL) == 0);
    CONTROLLA(info(d, a).rect.w == 300 && info(d, a).rect.h == 240); /* non torna a 540x340 */
    CONTROLLA(info(d, b).rect.w == 540); /* quella non toccata si', e lo fa da sola */
    /* gesto annullato: la misura raggiunta e' quella voluta */
    r = info(d, b).rect;
    CONTROLLA(ph_fin_gesto_inizia(d, b, PH_FIN_GESTO_RIDIMENSIONA, r.x + r.w - 3, r.y + r.h - 3, 20, NULL) == 0);
    CONTROLLA(ph_fin_gesto_muovi(d, r.x + r.w - 3 - 50, r.y + r.h - 3, 21, NULL) == 0);
    ph_fin_gesto_annulla(d, 22);
    CONTROLLA(info(d, b).rect.w == 490);
    CONTROLLA(ph_fin_imposta_host(d, 400, 348, 1.0, 23, NULL) == 0);
    CONTROLLA(ph_fin_imposta_host(d, 1280, 748, 1.0, 24, NULL) == 0);
    CONTROLLA(info(d, b).rect.w == 490);
    /* minimizzata durante il cambio: al ritorno ha la misura voluta */
    CONTROLLA(ph_fin_minimizza(d, c, 30, NULL) == 0);
    CONTROLLA(ph_fin_imposta_host(d, 400, 348, 1.0, 31, NULL) == 0);
    CONTROLLA(info(d, c).rect.w == 400);
    CONTROLLA(ph_fin_imposta_host(d, 1280, 748, 1.0, 32, NULL) == 0);
    CONTROLLA(ph_fin_ripristina(d, c, 33, NULL) == 0);
    CONTROLLA(info(d, c).rect.w == 540 && info(d, c).rect.h == 340);
    /* ingrandita durante il cambio: ripristinando torna la misura voluta */
    CONTROLLA(ph_fin_massimizza_ripristina(d, c, 40, NULL) == 0);
    CONTROLLA(ph_fin_imposta_host(d, 400, 348, 1.0, 41, NULL) == 0);
    CONTROLLA(info(d, c).rect.w == 400 && info(d, c).stato == PH_FIN_MASSIMIZZATA);
    CONTROLLA(ph_fin_imposta_host(d, 1280, 748, 1.0, 42, NULL) == 0);
    CONTROLLA(info(d, c).rect.w == 1280);
    CONTROLLA(ph_fin_massimizza_ripristina(d, c, 43, NULL) == 0);
    CONTROLLA(info(d, c).rect.w == 540 && info(d, c).rect.h == 340);
    /* rotazione: la misura ruotata e' la voluta */
    CONTROLLA(ph_fin_ruota(d, c, 50, NULL) == 0);
    int rw = info(d, c).rect.w, rh = info(d, c).rect.h;
    CONTROLLA(ph_fin_imposta_host(d, 400, 348, 1.0, 51, NULL) == 0);
    CONTROLLA(ph_fin_imposta_host(d, 1280, 748, 1.0, 52, NULL) == 0);
    CONTROLLA(info(d, c).rect.w == rw && info(d, c).rect.h == rh);
    ph_fin_libera(d);

    /* regola automatica (assestamento): torna esattamente alla stessa misura assestata, e UNA richiesta a movimento assestato */
    d = ph_fin_crea(1280, 748, 1.0, NULL);
    CONTROLLA(d != NULL);
    if (d) {
        CONTROLLA(ph_fin_imposta_qualita(d, 450, 160, 2560, 0, NULL) == 0);
        PhFinSpec sa = { 541, 343, false, false };
        unsigned id = ph_fin_apri(d, "x", "x", &sa, 0, NULL, NULL);
        PhFinRect r0 = info(d, id).rect;
        PhFinRichiesta rq[4];
        CONTROLLA(ph_fin_imposta_host(d, 400, 348, 1.0, 1000, NULL) == 0);
        CONTROLLA(info(d, id).rect.w <= 400 && info(d, id).rect.h <= 300);
        CONTROLLA(ph_fin_avanza(d, 1299, rq, 4) == 0);
        CONTROLLA(ph_fin_avanza(d, 1300, rq, 4) == 1 && rq[0].id == id);
        CONTROLLA(ph_fin_imposta_host(d, 1280, 748, 1.0, 2000, NULL) == 0);
        CONTROLLA(info(d, id).rect.w == r0.w && info(d, id).rect.h == r0.h);
        CONTROLLA(ph_fin_avanza(d, 2299, rq, 4) == 0);
        CONTROLLA(ph_fin_avanza(d, 2300, rq, 4) == 1 && rq[0].id == id && rq[0].w == info(d, id).utile_w);
        CONTROLLA(ph_fin_avanza(d, 9000, rq, 4) == 0);
        ph_fin_libera(d);
    }

    /* una finestra che sporge non viene ridotta: la misura resta (e quella voluta e' sempre la stessa) */
    d = nuovo(1280, 748, 1.0);
    unsigned s1 = ph_fin_apri(d, "s", "s", &sp, 0, NULL, NULL);
    metti_in(d, s1, 1100, 100);
    CONTROLLA(ph_fin_imposta_host(d, 1000, 748, 1.0, 1, NULL) == 0);
    CONTROLLA(info(d, s1).rect.w == 540 && info(d, s1).rect.x == 1000 - PH_FIN_VISIBILE_MIN);
    ph_fin_libera(d);
}


/* La misura voluta si salva (e si riapre) intera; all'apertura in un'area piccola e' quella chiesta. */
static void prova_voluta_salvata(void)
{
    PhFinSpec sp = { 540, 340, false, false };
    /* (1) chiusa mentre l'area e' stretta: "riapri dove eri" ricorda la misura voluta */
    PhFinDesktop *d = nuovo(1280, 748, 1.0);
    ph_fin_riapri_dove_eri(d, true);
    unsigned a = ph_fin_apri(d, "a", "a", &sp, 0, NULL, NULL);
    CONTROLLA(ph_fin_imposta_host(d, 400, 348, 1.0, 1, NULL) == 0);
    CONTROLLA(info(d, a).rect.w == 400 && info(d, a).rect.h == 300);
    PhFinRicordo mem[4];
    CONTROLLA(ph_fin_esporta(d, mem, 4) == 1 && mem[0].w == 540 && mem[0].h == 340); /* esportato con l'area stretta */
    CONTROLLA(ph_fin_chiudi(d, a, 2, NULL) == 0);
    CONTROLLA(ph_fin_esporta(d, mem, 4) == 1 && mem[0].w == 540 && mem[0].h == 340); /* ricordo di una finestra chiusa */
    CONTROLLA(ph_fin_imposta_host(d, 1280, 748, 1.0, 3, NULL) == 0);
    a = ph_fin_apri(d, "a", "a", &sp, 4, NULL, NULL);
    CONTROLLA(info(d, a).rect.w == 540 && info(d, a).rect.h == 340);
    ph_fin_libera(d);

    /* (2) aperta in un'area piccola: la misura voluta e' la chiesta, la finestra cresce quando l'area ricresce */
    d = nuovo(400, 348, 1.0); /* area 400x300 */
    unsigned b = ph_fin_apri(d, "b", "b", &sp, 0, NULL, NULL);
    CONTROLLA(info(d, b).rect.w == 400 && info(d, b).rect.h == 300);
    CONTROLLA(ph_fin_imposta_host(d, 1280, 748, 1.0, 1, NULL) == 0);
    CONTROLLA(info(d, b).rect.w == 540 && info(d, b).rect.h == 340);
    /* ... anche con il minimo (spec piu' piccola del minimo 240x180) */
    PhFinSpec piccola = { 100, 100, false, false };
    unsigned p = ph_fin_apri(d, "p", "p", &piccola, 2, NULL, NULL);
    CONTROLLA(info(d, p).rect.w == PH_FIN_MIN_W && info(d, p).rect.h == PH_FIN_MIN_H);
    ph_fin_libera(d);

    /* ricordo importato piu' grande dell'area: apertura in area piccola, poi cresce al ricordo */
    d = nuovo(400, 348, 1.0);
    ph_fin_riapri_dove_eri(d, true);
    PhFinRicordo ric;
    memset(&ric, 0, sizeof ric);
    strcpy(ric.app, "r");
    ric.x = 10; ric.y = 10; ric.w = 700; ric.h = 450;
    CONTROLLA(ph_fin_importa(d, &ric, 1) == 1);
    unsigned r = ph_fin_apri(d, "r", "r", &sp, 0, NULL, NULL);
    CONTROLLA(info(d, r).rect.w == 400 && info(d, r).rect.h == 300);
    CONTROLLA(ph_fin_imposta_host(d, 1280, 748, 1.0, 1, NULL) == 0);
    CONTROLLA(info(d, r).rect.w == 700 && info(d, r).rect.h == 450);
    ph_fin_libera(d);
}


/* ---- Compatibilita': densita' VERA del telefono, flusso allargato (CONTRATTI.md 19) ---- */
static int arr8(double v) { return (int)lround(v / 8.0) * 8; }

static PhFinDesktop *desk_compat(bool telefono_noto)
{
    PhFinDesktop *d = ph_fin_crea(1920, 1200, 1.0, NULL);
    CONTROLLA(d != NULL);
    if (!d) exit(1);
    CONTROLLA(ph_fin_imposta_qualita(d, 450, 160, 2560, 0, NULL) == 0);
    if (telefono_noto) CONTROLLA(ph_fin_imposta_telefono(d, 1080, 2340, 450, 0, NULL) == 0);
    return d;
}

static void prova_compatibilita(void)
{
    PhErrore e;
    PhFinRichiesta rq[4];
    /* grandezza di telefono (384x832 punti) a 450 dpi: circa 1080x2340 (multipli di 8: 1080x2344), densita' 450 */
    PhFinDesktop *d = desk_compat(true);
    PhFinSpec sg = ph_fin_spec_predefinita(false, false);
    unsigned g = ph_fin_apri(d, "g", "g", &sg, 0, NULL, NULL);
    PhFinSpec sv = ph_fin_spec_predefinita(true, false);
    unsigned v = ph_fin_apri(d, "v", "v", &sv, 0, NULL, NULL); /* solo verticale */
    PhFinInfo prima = info(d, g);
    CONTROLLA(prima.densita == 160 && prima.flusso_w == 384 && prima.flusso_h == 832 && !ph_fin_compatibilita(d, g));
    PhFinInfo prima_v = info(d, v);
    (void)ph_fin_avanza(d, 500, rq, 4);
    CONTROLLA(ph_fin_imposta_compatibilita(d, g, true, 1000, NULL) == 0 && ph_fin_compatibilita(d, g));
    PhFinInfo in = info(d, g);
    CONTROLLA(in.densita == 450 && in.fattore == 1.0 && in.flusso_w == 1080 && in.flusso_h == arr8(832 * 450 / 160.0));
    CONTROLLA(in.utile_w == 0 && in.utile_h == 0 && in.punti_w == 384 && in.rect.w == prima.rect.w && in.rect.h == prima.rect.h);
    /* nessun effetto sulle altre finestre */
    PhFinInfo dopo_v = info(d, v);
    CONTROLLA(dopo_v.densita == prima_v.densita && dopo_v.flusso_w == prima_v.flusso_w && dopo_v.flusso_h == prima_v.flusso_h);
    /* la richiesta parte SUBITO, con misura e densita' nuove, una volta sola */
    CONTROLLA(ph_fin_avanza(d, 1000, rq, 4) == 1 && rq[0].id == g && rq[0].densita == 450 && rq[0].w == 1080 && rq[0].h == in.flusso_h);
    CONTROLLA(ph_fin_avanza(d, 1001, rq, 4) == 0);
    /* stesso valore: nessuna richiesta */
    CONTROLLA(ph_fin_imposta_compatibilita(d, g, true, 1100, NULL) == 0 && ph_fin_avanza(d, 1101, rq, 4) == 0);
    /* solo verticale: la forma fissa segue il contenuto */
    CONTROLLA(ph_fin_imposta_compatibilita(d, v, true, 1200, NULL) == 0);
    CONTROLLA(info(d, v).densita == 450 && info(d, v).flusso_w == 1080 && info(d, v).solo_verticale);
    CONTROLLA(ph_fin_avanza(d, 1200, rq, 4) == 1 && rq[0].id == v);
    /* acceso -> spento: torna la regola automatica, richiesta immediata; poi di nuovo acceso */
    CONTROLLA(ph_fin_imposta_compatibilita(d, g, false, 2000, NULL) == 0 && !ph_fin_compatibilita(d, g));
    in = info(d, g);
    CONTROLLA(in.densita == 160 && in.utile_w > 0 && in.flusso_w == in.utile_w);
    CONTROLLA(ph_fin_avanza(d, 2000, rq, 4) == 1 && rq[0].id == g && rq[0].densita == 160);
    CONTROLLA(ph_fin_imposta_compatibilita(d, g, true, 3000, NULL) == 0);
    CONTROLLA(ph_fin_avanza(d, 3000, rq, 4) == 1 && rq[0].densita == 450 && rq[0].w == 1080);
    /* conversione delle coordinate andata e ritorno: immagine non 1:1, riempie tutto il contenuto */
    PhFinRect cr;
    CONTROLLA(ph_fin_contenuto(d, g, &cr, NULL) == 0);
    CONTROLLA(ph_fin_porta_davanti(d, g, NULL) == 0); /* davanti alla solo-verticale aperta dopo */
    CONTROLLA(ph_fin_dichiara_video(d, g, 1080, info(d, g).flusso_h, NULL) == 0);
    PhFinColpo col;
    for (int k = 0; k < 20; k++) {
        double x = cr.x + 7.3 + k * 18.9, y = cr.y + 5.1 + k * 40.7;
        ph_fin_trova(d, x, y, &col);
        CONTROLLA(col.id == g && col.zona == PH_FIN_ZONA_CONTENUTO && col.valido);
        CONTROLLA(fabs(col.vx - (x - cr.x) * 1080.0 / cr.w) < 1e-9 && fabs(col.vy - (y - cr.y) * info(d, g).flusso_h / (double)cr.h) < 1e-9);
        double xr = cr.x + col.vx * cr.w / 1080.0, yr = cr.y + col.vy * cr.h / (double)info(d, g).flusso_h;
        CONTROLLA(fabs(xr - x) < 1e-6 && fabs(yr - y) < 1e-6); /* ritorno */
    }
    ph_fin_trova(d, cr.x + cr.w - 0.01, cr.y + 10, &col); /* l'ultimo punto e' ancora nel video */
    CONTROLLA(col.valido && col.vx < 1080.0 && col.vx > 1079.0);
    ph_fin_trova(d, cr.x + 10, cr.y + cr.h - 0.01, &col);
    CONTROLLA(col.valido && col.vy < info(d, g).flusso_h && col.vy > info(d, g).flusso_h - 1.0);
    /* rifiuti: id sconosciuto, specchio, registrazione (nulla cambia) */
    CONTROLLA(ph_fin_imposta_compatibilita(d, 9999, true, 4000, &e) == -1 && e.testo[0]);
    PhFinSpec sq = ph_fin_spec_predefinita(false, true);
    unsigned sp = ph_fin_apri(d, "s", "s", &sq, 4000, NULL, NULL);
    CONTROLLA(ph_fin_imposta_compatibilita(d, sp, true, 4001, &e) == -1 && e.testo[0] && !ph_fin_compatibilita(d, sp));
    CONTROLLA(info(d, sp).densita == 0 && info(d, sp).flusso_w == 0);
    CONTROLLA(ph_fin_compatibilita(d, 9999) == false);
    PhFinSpec sx = { 400, 332, false, false };
    unsigned r = ph_fin_apri(d, "r", "r", &sx, 4000, NULL, NULL);
    CONTROLLA(ph_fin_imposta_registrazione(d, r, true, 4002, NULL) == 0);
    PhFinInfo ir = info(d, r);
    CONTROLLA(ph_fin_imposta_compatibilita(d, r, true, 4003, &e) == -1 && e.testo[0] && !ph_fin_compatibilita(d, r));
    CONTROLLA(info(d, r).densita == ir.densita && info(d, r).flusso_w == ir.flusso_w);
    CONTROLLA(ph_fin_imposta_registrazione(d, r, false, 4004, NULL) == 0);
    /* idempotenza dell'assestamento dopo lo spegnimento: piede acceso due volte di fila non cambia nulla */
    CONTROLLA(ph_fin_imposta_compatibilita(d, r, true, 5000, NULL) == 0 && ph_fin_imposta_compatibilita(d, r, false, 5001, NULL) == 0);
    CONTROLLA(ph_fin_imposta_pie(d, true, 5002, NULL) == 0);
    PhFinRect r1 = info(d, r).rect;
    CONTROLLA(ph_fin_imposta_pie(d, true, 5003, NULL) == 0);
    CONTROLLA(uguale(info(d, r).rect, r1.x, r1.y, r1.w, r1.h));
    CONTROLLA(ph_fin_imposta_pie(d, false, 5004, NULL) == 0);
    /* minimizzata: il cambio si vede al ripristino, la richiesta e' immediata */
    CONTROLLA(ph_fin_minimizza(d, r, 6000, NULL) == 0);
    (void)ph_fin_avanza(d, 6001, rq, 4);
    CONTROLLA(ph_fin_imposta_compatibilita(d, r, true, 6002, NULL) == 0);
    CONTROLLA(ph_fin_avanza(d, 6003, rq, 4) == 0);
    CONTROLLA(ph_fin_ripristina(d, r, 6010, NULL) == 0);
    CONTROLLA(ph_fin_avanza(d, 6010, rq, 4) == 1 && rq[0].id == r && rq[0].densita == 450);
    ph_fin_libera(d);

    /* finestra stretta (288 punti < 384): f = 0,75, densita' 450 invariata, flusso allargato: l'app vede 384 punti */
    d = desk_compat(true);
    PhFinSpec ss = { 288, 632, false, false };
    unsigned s1 = ph_fin_apri(d, "x", "x", &ss, 0, NULL, NULL);
    PhFinRect c1;
    CONTROLLA(ph_fin_contenuto(d, s1, &c1, NULL) == 0 && c1.w == 288);
    CONTROLLA(ph_fin_imposta_compatibilita(d, s1, true, 1, NULL) == 0);
    in = info(d, s1);
    CONTROLLA(in.densita == 450 && in.fattore == 0.75 && in.flusso_w == 1080 && in.flusso_h == arr8(c1.h / 0.75 * 450 / 160.0));
    CONTROLLA(in.punti_w == 384);
    /* ridimensionamento con la voce attiva: richieste a fine assestamento (300 ms), sempre con D */
    PhFinRect rr = in.rect;
    (void)ph_fin_avanza(d, 100, rq, 4);
    CONTROLLA(ph_fin_gesto_inizia(d, s1, PH_FIN_GESTO_RIDIMENSIONA, rr.x + rr.w - 3, rr.y + rr.h - 3, 1000, NULL) == 0);
    CONTROLLA(ph_fin_gesto_muovi(d, rr.x + rr.w - 3 + 200, rr.y + rr.h - 3, 1100, NULL) == 0);
    CONTROLLA(ph_fin_avanza(d, 1500, rq, 4) == 0);
    CONTROLLA(ph_fin_gesto_finisci(d, rr.x + rr.w - 3 + 200, rr.y + rr.h - 3, 2000, NULL, NULL) == 0);
    CONTROLLA(ph_fin_avanza(d, 2299, rq, 4) == 0);
    CONTROLLA(ph_fin_avanza(d, 2300, rq, 4) == 1 && rq[0].densita == 450 && rq[0].w == info(d, s1).flusso_w);
    CONTROLLA(info(d, s1).rect.w == 488 && info(d, s1).fattore == 1.0 && info(d, s1).flusso_w == arr8(488 * 450 / 160.0));
    ph_fin_libera(d);

    /* oltre il tetto: lato lungo = tetto, la densita' RESTA 450, i punti si riducono (<= 910 sul lato lungo) */
    d = desk_compat(true);
    PhFinSpec sb = { 1500, 732, false, false };
    unsigned b = ph_fin_apri(d, "b", "b", &sb, 0, NULL, NULL);
    CONTROLLA(ph_fin_imposta_compatibilita(d, b, true, 1, NULL) == 0);
    in = info(d, b);
    CONTROLLA(in.densita == 450 && in.flusso_w == 2560 && in.flusso_h < 2560 && in.punti_w <= 910 && in.punti_w >= 909);
    CONTROLLA((double)in.flusso_h / in.flusso_w > in.rect.h / (double)in.rect.w * 0.9 - 0.2); /* proporzioni circa conservate */
    ph_fin_libera(d);

    /* densita' del telefono non nota: vale quella di ph_fin_imposta_qualita; f = 1 */
    d = desk_compat(false);
    CONTROLLA(ph_fin_imposta_qualita(d, 420, 160, 2560, 0, NULL) == 0);
    PhFinSpec sn = { 288, 632, false, false };
    unsigned n = ph_fin_apri(d, "n", "n", &sn, 0, NULL, NULL);
    PhFinRect cn;
    CONTROLLA(ph_fin_contenuto(d, n, &cn, NULL) == 0);
    CONTROLLA(ph_fin_imposta_compatibilita(d, n, true, 1, NULL) == 0);
    in = info(d, n);
    CONTROLLA(in.densita == 420 && in.fattore == 1.0 && in.flusso_w == arr8(cn.w * 420 / 160.0) && in.flusso_h == arr8(cn.h * 420 / 160.0));
    ph_fin_libera(d);

    /* regola globale FISSA con un'altra densita': in compatibilita' vince quella del telefono, le altre finestre no */
    d = desk_compat(true);
    CONTROLLA(ph_fin_imposta_qualita(d, 320, 160, 2560, 0, NULL) == 0);
    CONTROLLA(ph_fin_imposta_densita_automatica(d, false, 0, NULL) == 0);
    PhFinSpec sf = { 400, 332, false, false };
    unsigned f1 = ph_fin_apri(d, "f1", "f1", &sf, 0, NULL, NULL), f2 = ph_fin_apri(d, "f2", "f2", &sf, 0, NULL, NULL);
    CONTROLLA(info(d, f1).densita == 320 && info(d, f2).densita == 320);
    CONTROLLA(ph_fin_imposta_compatibilita(d, f1, true, 1, NULL) == 0);
    CONTROLLA(info(d, f1).densita == 450 && info(d, f2).densita == 320);
    ph_fin_libera(d);
}


/* Compatibilita' con contenuto non multiplo di 8 (ridimensionata a voce attiva): coordinate in proporzione sul contenuto intero;
 * spegnendo la voce il contenuto si assesta (e resta assestato). */
static void prova_compatibilita_assestamento(void)
{
    PhFinDesktop *d = desk_compat(true);
    PhFinSpec sp = { 401, 333, false, false };
    unsigned id = ph_fin_apri(d, "x", "x", &sp, 0, NULL, NULL);
    CONTROLLA(ph_fin_imposta_compatibilita(d, id, true, 1, NULL) == 0);
    PhFinRect r = info(d, id).rect;
    CONTROLLA(ph_fin_gesto_inizia(d, id, PH_FIN_GESTO_RIDIMENSIONA, r.x + r.w - 3, r.y + r.h - 3, 10, NULL) == 0);
    CONTROLLA(ph_fin_gesto_muovi(d, r.x + r.w - 3 + 37, r.y + r.h - 3 + 13, 11, NULL) == 0);
    CONTROLLA(ph_fin_gesto_finisci(d, r.x + r.w - 3 + 37, r.y + r.h - 3 + 13, 12, NULL, NULL) == 0);
    PhFinRect c;
    CONTROLLA(ph_fin_contenuto(d, id, &c, NULL) == 0);
    CONTROLLA(c.w % 8 != 0 && info(d, id).utile_w == 0); /* a voce attiva niente assestamento */
    PhFinInfo in = info(d, id);
    CONTROLLA(ph_fin_dichiara_video(d, id, in.flusso_w, in.flusso_h, NULL) == 0);
    PhFinColpo col;
    for (int k = 0; k < 10; k++) {
        double x = c.x + 3.7 + k * 11.3, y = c.y + 2.9 + k * 17.1;
        ph_fin_trova(d, x, y, &col);
        CONTROLLA(col.valido && fabs(col.vx - (x - c.x) * in.flusso_w / (double)c.w) < 1e-9);
        CONTROLLA(fabs(col.vy - (y - c.y) * in.flusso_h / (double)c.h) < 1e-9);
    }
    /* spenta: torna la regola automatica, il contenuto si assesta ai multipli di 8 pixel fisici, e vi resta */
    CONTROLLA(ph_fin_imposta_compatibilita(d, id, false, 1000, NULL) == 0);
    CONTROLLA(ph_fin_contenuto(d, id, &c, NULL) == 0);
    in = info(d, id);
    CONTROLLA(c.w % 8 == 0 && in.utile_w == c.w && in.flusso_w == in.utile_w);
    PhFinRect r1 = in.rect;
    CONTROLLA(ph_fin_imposta_pie(d, true, 1001, NULL) == 0);
    PhFinRect r2 = info(d, id).rect;
    CONTROLLA(ph_fin_imposta_pie(d, true, 1002, NULL) == 0); /* idempotente */
    CONTROLLA(uguale(info(d, id).rect, r2.x, r2.y, r2.w, r2.h));
    CONTROLLA(r1.w == r2.w);
    ph_fin_libera(d);
}

/* Clic fermo, puntatore fuori misura, specchio, registrazione, ruota. */
static void prova_nuove_regole(void)
{
    PhFinDesktop *d = nuovo(980, 584, 1.0);
    unsigned n = apri(d, "note", 0);
    PhFinAggancio ag = PH_FIN_AGG_A_DESTRA;
    /* un clic (inizia + finisci nello stesso punto) su una agganciata NON la stacca */
    CONTROLLA(ph_fin_gesto_inizia(d, n, PH_FIN_GESTO_SPOSTA, 100, 40, 0, NULL) == 0);
    CONTROLLA(ph_fin_gesto_finisci(d, 3, 200, 1, &ag, NULL) == 0 && ag == PH_FIN_AGG_A_SINISTRA);
    CONTROLLA(ph_fin_gesto_inizia(d, n, PH_FIN_GESTO_SPOSTA, 98, 10, 2, NULL) == 0);
    CONTROLLA(ph_fin_gesto_finisci(d, 98, 10, 3, &ag, NULL) == 0 && ag == PH_FIN_AGG_NESSUNO);
    CONTROLLA(info(d, n).stato == PH_FIN_AGG_SX && uguale(info(d, n).rect, 0, 0, 490, 536));
    /* anche con un muovi nello stesso punto prima */
    CONTROLLA(ph_fin_gesto_inizia(d, n, PH_FIN_GESTO_SPOSTA, 98, 10, 4, NULL) == 0);
    CONTROLLA(ph_fin_gesto_muovi(d, 98, 10, 5, NULL) == 0);
    CONTROLLA(info(d, n).stato == PH_FIN_AGG_SX);
    CONTROLLA(ph_fin_gesto_finisci(d, 98, 10, 6, NULL, NULL) == 0 && info(d, n).stato == PH_FIN_AGG_SX);
    /* un clic fermo vicino al bordo alto di una finestra normale non la massimizza */
    CONTROLLA(ph_fin_massimizza_ripristina(d, n, 7, NULL) == 0 && ph_fin_massimizza_ripristina(d, n, 7, NULL) == 0);
    CONTROLLA(info(d, n).stato == PH_FIN_AGG_SX); /* prima massimizza poi torna a come era */
    CONTROLLA(ph_fin_gesto_inizia(d, n, PH_FIN_GESTO_SPOSTA, 100, 10, 8, NULL) == 0);
    CONTROLLA(ph_fin_gesto_finisci(d, 300, 200, 9, &ag, NULL) == 0 && info(d, n).stato == PH_FIN_NORMALE);
    PhFinRect r = info(d, n).rect;
    CONTROLLA(ph_fin_gesto_inizia(d, n, PH_FIN_GESTO_SPOSTA, r.x + 10, r.y + 5, 10, NULL) == 0);
    CONTROLLA(ph_fin_gesto_muovi(d, r.x + 10, 5 - 0, 11, NULL) == 0); /* si muove verso l'alto */
    CONTROLLA(ph_fin_gesto_finisci(d, r.x + 10, r.y + 5, 12, &ag, NULL) == 0); /* torna al punto d'inizio */
    CONTROLLA(ag == PH_FIN_AGG_NESSUNO && info(d, n).stato == PH_FIN_NORMALE);

    /* puntatore enorme o non numerico: limitato, nessun comportamento indefinito */
    CONTROLLA(ph_fin_gesto_inizia(d, n, PH_FIN_GESTO_SPOSTA, 1e300, -1e300, 20, NULL) == 0);
    CONTROLLA(ph_fin_gesto_muovi(d, -1e300, 1e300, 21, NULL) == 0);
    CONTROLLA(ph_fin_gesto_muovi(d, 1e18, 1e18, 22, NULL) == 0);
    CONTROLLA(ph_fin_gesto_muovi(d, 0.0 / 0.0, 1, 23, NULL) == -1);
    CONTROLLA(info(d, n).rect.x <= 980 - PH_FIN_VISIBILE_MIN && info(d, n).rect.y <= 536 - PH_FIN_TITOLO_H);
    ph_fin_gesto_annulla(d, 24);
    CONTROLLA(ph_fin_gesto_inizia(d, n, PH_FIN_GESTO_RIDIMENSIONA, 1e300, 1e300, 25, NULL) == 0);
    CONTROLLA(ph_fin_gesto_muovi(d, -1e300, -1e300, 26, NULL) == 0);
    CONTROLLA(ph_fin_gesto_muovi(d, 1e300, 1e300, 27, NULL) == 0);
    CONTROLLA(info(d, n).rect.w >= 1 && info(d, n).rect.x + info(d, n).rect.w <= 980);
    ph_fin_gesto_annulla(d, 28);
    ph_fin_libera(d);

    /* ---- specchio dello schermo reale: nessuna misura, nessuna richiesta ---- */
    d = nuovo(980, 584, 1.0);
    CONTROLLA(ph_fin_imposta_qualita(d, 420, 160, 2560, 0, NULL) == 0);
    unsigned p = apri(d, "phone", 0);
    CONTROLLA(info(d, p).specchio && info(d, p).flusso_w == 0 && info(d, p).flusso_h == 0);
    int w, h;
    PhErrore e;
    CONTROLLA(ph_fin_misura_flusso(d, p, &w, &h, &e) == -1 && e.testo[0]);
    CONTROLLA(!info(d, n = apri(d, "note", 0)).specchio);
    CONTROLLA(ph_fin_massimizza_ripristina(d, p, 10, NULL) == -1); /* solo verticale: non si ingrandisce */
    CONTROLLA(ph_fin_massimizza_ripristina(d, n, 10, NULL) == 0);
    CONTROLLA(ph_fin_imposta_host(d, 1280, 800, 1.0, 11, NULL) == 0);
    PhFinRichiesta rq[4];
    size_t k = ph_fin_avanza(d, 20000, rq, 4);
    for (size_t i = 0; i < k; i++) CONTROLLA(rq[i].id != p);
    CONTROLLA(k == 1 && rq[0].id == n); /* la finestra normale si, lo specchio mai */
    CONTROLLA(ph_fin_avanza(d, 40000, rq, 4) == 0);
    ph_fin_libera(d);

    /* ---- registrazione: la dimensione non cambia ---- */
    d = nuovo(980, 584, 1.0);
    CONTROLLA(ph_fin_imposta_qualita(d, 420, 160, 2560, 0, NULL) == 0);
    CONTROLLA(ph_fin_imposta_densita_automatica(d, false, 0, NULL) == 0); /* regola vecchia: densita' fissa */
    n = apri(d, "note", 0);
    CONTROLLA(ph_fin_imposta_registrazione(d, n, true, 1, NULL) == 0 && info(d, n).registrando);
    CONTROLLA(ph_fin_gesto_inizia(d, n, PH_FIN_GESTO_RIDIMENSIONA, 465, 315, 2, &e) == -1 && e.testo[0]);
    CONTROLLA(ph_fin_massimizza_ripristina(d, n, 3, NULL) == -1 && info(d, n).stato == PH_FIN_NORMALE);
    CONTROLLA(ph_fin_ruota(d, n, 4, NULL) == -1);
    CONTROLLA(uguale(info(d, n).rect, 70, 30, 400, 290));
    /* lo spostamento si puo' fare ma l'aggancio no */
    CONTROLLA(ph_fin_gesto_inizia(d, n, PH_FIN_GESTO_SPOSTA, 100, 40, 5, NULL) == 0);
    CONTROLLA(ph_fin_gesto_muovi(d, 5, 300, 6, NULL) == 0);
    CONTROLLA(ph_fin_gesto_anteprima(d, NULL) == PH_FIN_AGG_NESSUNO);
    CONTROLLA(ph_fin_gesto_finisci(d, 5, 300, 7, &ag, NULL) == 0 && ag == PH_FIN_AGG_NESSUNO);
    CONTROLLA(info(d, n).stato == PH_FIN_NORMALE && info(d, n).rect.w == 400 && info(d, n).rect.h == 290);
    /* se la misura cambiasse comunque (densita'), niente richieste finche' registra */
    CONTROLLA(ph_fin_imposta_qualita(d, 320, 160, 2560, 10, NULL) == 0);
    CONTROLLA(ph_fin_avanza(d, 20000, rq, 4) == 0);
    CONTROLLA(ph_fin_imposta_registrazione(d, n, false, 20000, NULL) == 0);
    CONTROLLA(ph_fin_avanza(d, 20299, rq, 4) == 0);
    CONTROLLA(ph_fin_avanza(d, 20300, rq, 4) == 1 && rq[0].id == n);
    CONTROLLA(ph_fin_imposta_registrazione(d, 999, true, 0, NULL) == -1);
    /* registrando su una agganciata: non si stacca */
    CONTROLLA(ph_fin_gesto_inizia(d, n, PH_FIN_GESTO_SPOSTA, 100, 40, 30000, NULL) == 0);
    CONTROLLA(ph_fin_gesto_finisci(d, 3, 200, 30001, &ag, NULL) == 0 && ag == PH_FIN_AGG_A_SINISTRA);
    CONTROLLA(ph_fin_imposta_registrazione(d, n, true, 30002, NULL) == 0);
    CONTROLLA(ph_fin_gesto_inizia(d, n, PH_FIN_GESTO_SPOSTA, 100, 10, 30003, NULL) == -1);
    CONTROLLA(info(d, n).stato == PH_FIN_AGG_SX);
    ph_fin_libera(d);

    /* ---- ruota ---- */
    d = nuovo(980, 584, 1.0);
    n = apri(d, "note", 0); /* contenuto 400x220 */
    CONTROLLA(ph_fin_ruota(d, n, 0, NULL) == 0);
    CONTROLLA(uguale(info(d, n).rect, 70, 30, 288, 470)); /* contenuto 220x400 + 70; larghezza minima 288 */
    CONTROLLA(ph_fin_ruota(d, n, 1, NULL) == 0);
    CONTROLLA(uguale(info(d, n).rect, 70, 30, 400, 358)); /* contenuto 288x400 -> 400x288, +70 */
    /* una finestra in basso a destra che diventa piu' alta (rotazione) torna tutta dentro */
    CONTROLLA(ph_fin_gesto_inizia(d, n, PH_FIN_GESTO_SPOSTA, 100, 40, 2, NULL) == 0);
    CONTROLLA(ph_fin_gesto_finisci(d, 600, 400, 3, NULL, NULL) == 0);
    PhFinRect prima_r = info(d, n).rect;
    CONTROLLA(ph_fin_ruota(d, n, 4, NULL) == 0);
    r = info(d, n).rect;
    CONTROLLA(r.w == 288 && r.h == 470 && r.y >= 0 && r.y + r.h <= 536 && r.x + r.w <= 980); /* la rotazione la riporta tutta dentro */
    (void)prima_r;
    /* piu' grande dell'area: ridotta */
    CONTROLLA(ph_fin_imposta_host(d, 500, 300, 1.0, 5, NULL) == 0); /* area 500x252 */
    CONTROLLA(ph_fin_ruota(d, n, 6, NULL) == 0);
    r = info(d, n).rect;
    CONTROLLA(r.w >= 1 && r.h >= 1 && r.w <= 500 && r.h <= 252 && r.y >= 0 && r.y <= 252 - PH_FIN_TITOLO_H);
    CONTROLLA(ph_fin_imposta_host(d, 980, 584, 1.0, 7, NULL) == 0);
    /* richiesta dopo l'assestamento */
    PhFinDesktop *d3 = nuovo(980, 584, 1.0);
    unsigned n3 = apri(d3, "note", 0);
    CONTROLLA(ph_fin_ruota(d3, n3, 100, NULL) == 0);
    CONTROLLA(ph_fin_avanza(d3, 399, rq, 4) == 0);
    CONTROLLA(ph_fin_avanza(d3, 400, rq, 4) == 1 && rq[0].id == n3 && rq[0].w == 288 && rq[0].h == 400); /* 220 -> 288 (minimo), gia' multiplo di 8 */
    ph_fin_libera(d3);
    /* rifiuti: massimizzata, agganciata, minimizzata, verticale, inesistente */
    CONTROLLA(ph_fin_massimizza_ripristina(d, n, 500, NULL) == 0);
    CONTROLLA(ph_fin_ruota(d, n, 501, &e) == -1 && e.testo[0]);
    CONTROLLA(ph_fin_massimizza_ripristina(d, n, 502, NULL) == 0);
    CONTROLLA(ph_fin_minimizza(d, n, 503, NULL) == 0);
    CONTROLLA(ph_fin_ruota(d, n, 504, NULL) == -1);
    CONTROLLA(ph_fin_ripristina(d, n, 505, NULL) == 0);
    CONTROLLA(ph_fin_gesto_inizia(d, n, PH_FIN_GESTO_SPOSTA, 100, 40, 506, NULL) == 0);
    CONTROLLA(ph_fin_gesto_finisci(d, 3, 200, 507, &ag, NULL) == 0 && ag == PH_FIN_AGG_A_SINISTRA);
    CONTROLLA(ph_fin_ruota(d, n, 508, NULL) == -1);
    unsigned m = apri(d, "msg", 509);
    CONTROLLA(ph_fin_ruota(d, m, 510, NULL) == -1);
    /* specchio NON solo-verticale: rifiutato comunque */
    PhFinSpec sp = { 400, 300, false, true };
    unsigned sq = ph_fin_apri(d, "specchio2", "Specchio", &sp, 510, NULL, NULL);
    CONTROLLA(sq != 0 && info(d, sq).specchio && !info(d, sq).solo_verticale);
    CONTROLLA(ph_fin_ruota(d, sq, 511, &e) == -1 && e.testo[0]);
    CONTROLLA(info(d, sq).rect.w == 400 && info(d, sq).rect.h == 300);
    CONTROLLA(ph_fin_ruota(d, 4242, 511, NULL) == -1);
    ph_fin_libera(d);

    /* minimi dopo lo scambio: 288x180 a piede spento (contenuto 288x130) darebbe 130 di larghezza */
    d = ph_fin_crea(980, 584, 1.0, NULL);
    CONTROLLA(d != NULL);
    if (d) {
        PhFinSpec piccola = { 288, 180, false, false };
        unsigned q = ph_fin_apri(d, "piccola", "P", &piccola, 0, NULL, NULL);
        CONTROLLA(q != 0 && info(d, q).rect.w == 288 && info(d, q).rect.h == 186); /* contenuto 288x130 -> 288x136 (multiplo di 8 piu' vicino sopra il minimo) */
        CONTROLLA(ph_fin_ruota(d, q, 1, NULL) == 0);
        CONTROLLA(info(d, q).rect.w >= PH_FIN_MIN_W && info(d, q).rect.h >= PH_FIN_MIN_H);
        CONTROLLA(info(d, q).rect.w == 288 && info(d, q).rect.h == 338); /* 288 di contenuto + 50 di titolo */
        /* area piu' piccola dei minimi: entro l'area */
        CONTROLLA(ph_fin_imposta_host(d, 200, 150, 1.0, 2, NULL) == 0); /* area 200x102 */
        CONTROLLA(ph_fin_ruota(d, q, 3, NULL) == 0);
        CONTROLLA(info(d, q).rect.w <= 200 && info(d, q).rect.h <= 102 && info(d, q).rect.w >= 1);
        ph_fin_libera(d);
    }

    /* registrare durante un ridimensionamento lo ferma */
    d = nuovo(980, 584, 1.0);
    n = apri(d, "note", 0);
    CONTROLLA(ph_fin_gesto_inizia(d, n, PH_FIN_GESTO_RIDIMENSIONA, 465, 315, 1, NULL) == 0);
    CONTROLLA(ph_fin_gesto_muovi(d, 565, 365, 2, NULL) == 0);
    CONTROLLA(ph_fin_imposta_registrazione(d, n, true, 3, NULL) == 0);
    CONTROLLA(ph_fin_gesto_finestra(d) == 0);
    CONTROLLA(ph_fin_gesto_muovi(d, 700, 400, 4, NULL) == -1);
    CONTROLLA(info(d, n).rect.w == 500 && info(d, n).rect.h == 340);
    ph_fin_libera(d);
}

/* ---------------------------------------------------------------- */
/* Test a caso: sequenze casuali di operazioni, invarianti sempre.   */

static uint32_t seme;
static uint32_t casuale(void)
{
    seme = seme * 1664525u + 1013904223u;
    return seme >> 8;
}
static double punto(int massimo_v) { return (double)((int)(casuale() % (uint32_t)(massimo_v * 3)) - massimo_v); }

static void invarianti(PhFinDesktop *d, uint32_t passo)
{
    PhFinRect a = ph_fin_area(d);
    size_t n = ph_fin_conta(d);
    CONTROLLA(n <= PH_FIN_MAX);
    unsigned ids[PH_FIN_MAX];
    CONTROLLA(ph_fin_ordine(d, ids, PH_FIN_MAX) == n);
    unsigned attiva = ph_fin_attiva(d);
    int n_attive = 0;
    bool attiva_esiste = attiva == 0;
    for (size_t i = 0; i < n; i++) {
        for (size_t j = i + 1; j < n; j++) CONTROLLA(ids[i] != ids[j]);
        PhFinInfo f;
        CONTROLLA(ph_fin_info(d, ids[i], &f, NULL) == 0);
        if (f.attiva) {
            n_attive++;
            CONTROLLA(f.id == attiva);
            CONTROLLA(!f.minimizzata);
            attiva_esiste = true;
        }
        CONTROLLA(f.rect.w >= 1 && f.rect.h >= 1);
        CONTROLLA(f.rect.y >= 0 && f.rect.y + (f.rect.h < PH_FIN_TITOLO_H ? f.rect.h : PH_FIN_TITOLO_H) <= a.h); /* la barra del titolo resta sopra il bordo inferiore */
        int vis = f.rect.w < PH_FIN_VISIBILE_MIN ? f.rect.w : PH_FIN_VISIBILE_MIN;
        CONTROLLA(f.rect.x + f.rect.w >= vis && f.rect.x <= a.w - vis);
        CONTROLLA(f.normale.w >= 1 && f.normale.h >= 1 && f.normale.w <= a.w && f.normale.h <= a.h);
        if (f.stato == PH_FIN_MASSIMIZZATA && !f.solo_verticale)
            CONTROLLA(uguale(f.rect, 0, 0, a.w, a.h));
        if (f.stato == PH_FIN_AGG_SX) CONTROLLA(f.rect.x == 0 && f.rect.h == a.h && f.rect.w >= 1);
        if (f.stato == PH_FIN_AGG_DX) CONTROLLA(f.rect.x + f.rect.w == a.w && f.rect.h == a.h);
        CONTROLLA(!(f.solo_verticale || f.specchio) || f.stato == PH_FIN_NORMALE); /* mai ingrandita ne' agganciata */
        CONTROLLA(f.utile_w % 8 == 0 && f.utile_h % 8 == 0 && (f.utile_w == 0) == (f.utile_h == 0));
        CONTROLLA(!(f.solo_verticale || f.specchio) || (!f.puo_ridimensionare && !f.puo_ingrandire && !f.puo_agganciare));
        CONTROLLA(f.specchio || (f.densita >= PH_FIN_DENSITA_MIN && f.densita <= PH_FIN_DENSITA_MAX));
        CONTROLLA(f.specchio || (f.flusso_w % 8 == 0 && f.flusso_h % 8 == 0));
        CONTROLLA(f.specchio || (f.flusso_w >= 64 && f.flusso_h >= 64 && f.flusso_w <= 2560 && f.flusso_h <= 2560));
        int w, h;
        if (f.specchio) {
            CONTROLLA(f.flusso_w == 0 && ph_fin_misura_flusso(d, ids[i], &w, &h, NULL) == -1);
            continue;
        }
        CONTROLLA(ph_fin_misura_flusso(d, ids[i], &w, &h, NULL) == 0 && w == f.flusso_w && h == f.flusso_h);
    }
    CONTROLLA(n_attive <= 1);
    CONTROLLA(attiva_esiste);
    unsigned g = ph_fin_gesto_finestra(d);
    if (g) {
        PhFinInfo f;
        CONTROLLA(ph_fin_info(d, g, &f, NULL) == 0 && !f.minimizzata);
    }
    if (errori) {
        fprintf(stderr, "(invarianti rotti al passo %u, seme corrente %u)\n", passo, seme);
        exit(1);
    }
}

/* Tiene d'occhio la misura del flusso di ogni finestra, per controllare da fuori la
 * regola dei 300 ms: una richiesta parte solo se l'ultimo cambio e' di almeno 300 ms
 * prima (l'osservazione da fuori e' sempre uguale o PIU' VECCHIA di quella interna,
 * quindi il controllo e' prudente) e mai due volte di fila per la stessa misura. */
typedef struct {
    int vw, vh;       /* ultima misura osservata */
    uint64_t tcamb;
    int qw, qh, qd;   /* ultima misura e densita' chieste */
    int vd;           /* ultima densita' osservata */
} Osserva;
static Osserva oss[4096];

static void osserva(const PhFinDesktop *d, uint64_t t)
{
    unsigned ids[PH_FIN_MAX];
    size_t n = ph_fin_ordine(d, ids, PH_FIN_MAX);
    for (size_t i = 0; i < n; i++) {
        PhFinInfo f;
        if (ph_fin_info(d, ids[i], &f, NULL) != 0 || ids[i] >= 4096 || f.specchio) continue;
        Osserva *o = &oss[ids[i]];
        if (o->vw != f.flusso_w || o->vh != f.flusso_h || o->vd != f.densita) {
            o->vd = f.densita;
            o->vw = f.flusso_w;
            o->vh = f.flusso_h;
            o->tcamb = t;
        }
    }
}

static void prova_a_caso(void)
{
    static const char *app[] = { "msg", "web", "note", "calc", "photo", "map", "music", "set", "phone",
                                 "a1", "a2", "a3", "a4", "a5", "a6", "a7", "a8", "a9", "a10", "a11" };
    for (uint32_t s = 1; s <= 25; s++) {
        seme = s * 7919u;
        memset(oss, 0, sizeof oss);
        PhFinDesktop *d = nuovo(980, 584, 1.0);
        uint64_t t = 0; /* il tempo non torna mai indietro */
        PhFinRichiesta rq[8];
        for (uint32_t passo = 0; passo < 1500; passo++) {
            t += casuale() % 200;
            size_t n = ph_fin_conta(d);
            unsigned ids[PH_FIN_MAX + 1];
            ph_fin_ordine(d, ids, PH_FIN_MAX);
            unsigned id = n && casuale() % 8 ? ids[casuale() % n] : casuale() % 100; /* a volte inesistente */
            PhFinRect a = ph_fin_area(d);
            switch (casuale() % 21) {
            case 0: case 1: {
                const char *nome = app[casuale() % (sizeof app / sizeof app[0])];
                PhFinSpec sp = ph_fin_spec_mockup(nome);
                if (strcmp(nome, "a2") == 0) { sp.solo_verticale = false; sp.specchio = true; sp.w = 350; sp.h = 250; } /* specchio senza il segno verticale */
                if (strcmp(nome, "a3") == 0) { sp = ph_fin_spec_predefinita(false, true); }
                (void)ph_fin_apri(d, nome, nome, &sp, t, NULL, NULL);
                break;
            }
            case 2: (void)ph_fin_chiudi(d, id, t, NULL); break;
            case 3: (void)ph_fin_minimizza(d, id, t, NULL); break;
            case 4: (void)ph_fin_ripristina(d, id, t, NULL); break;
            case 5: (void)ph_fin_massimizza_ripristina(d, id, t, NULL); break;
            case 6: (void)ph_fin_porta_davanti(d, id, NULL); break;
            case 7: (void)ph_fin_clic_barra(d, id, t, NULL, NULL); break;
            case 8: case 9:
                (void)ph_fin_gesto_inizia(d, id, casuale() % 2 ? PH_FIN_GESTO_SPOSTA : PH_FIN_GESTO_RIDIMENSIONA,
                                          punto(a.w), punto(a.h), t, NULL);
                break;
            case 10: case 11: case 12: {
                int k = (int)(casuale() % 4);
                for (int i = 0; i < k; i++) (void)ph_fin_gesto_muovi(d, punto(a.w + 200), punto(a.h + 200), t, NULL);
                break;
            }
            case 13:
                if (casuale() % 3) (void)ph_fin_gesto_finisci(d, punto(a.w), punto(a.h), t, NULL, NULL);
                else ph_fin_gesto_annulla(d, t);
                break;
            case 14: {
                int hw = casuale() % 6 == 0 ? (int)(casuale() % 150) + 1 : (int)(casuale() % 2500) + 1;
                int hh = casuale() % 6 == 0 ? (int)(casuale() % 150) + 1 : (int)(casuale() % 1500) + 1;
                double sc = 0.5 + (casuale() % 8) * 0.25;
                (void)ph_fin_imposta_host(d, hw, hh, sc, t, NULL);
                break;
            }
            case 15:
                (void)ph_fin_imposta_qualita(d, 100.0 + casuale() % 500, 160.0, 64 + (int)(casuale() % 2497), t, NULL);
                break;
            case 16: (void)ph_fin_ruota(d, id, t, NULL); break;
            case 20: (void)ph_fin_imposta_solo_verticale(d, id, casuale() % 2, t, NULL); break;
            case 17: (void)ph_fin_imposta_registrazione(d, id, casuale() % 2, t, NULL); break;
            case 18:
                if (casuale() % 2) (void)ph_fin_imposta_pie(d, casuale() % 2, t, NULL);
                else (void)ph_fin_imposta_densita_automatica(d, casuale() % 4 != 0, t, NULL);
                break;
            default:
                (void)ph_fin_dichiara_video(d, id, 1 + (int)(casuale() % 3000), 1 + (int)(casuale() % 3000), NULL);
                break;
            }
            osserva(d, t);
            invarianti(d, passo);
            if (casuale() % 3 == 0) {
                t += casuale() % 400;
                size_t k = ph_fin_avanza(d, t, rq, 8);
                for (size_t i = 0; i < k; i++) {
                    CONTROLLA(rq[i].w % 8 == 0 && rq[i].h % 8 == 0 && rq[i].w >= 64 && rq[i].h >= 64);
                    CONTROLLA(rq[i].id != ph_fin_gesto_finestra(d));
                    CONTROLLA(rq[i].id < 4096);
                    if (rq[i].id < 4096) {
                        Osserva *o = &oss[rq[i].id];
                        CONTROLLA(t - o->tcamb >= PH_FIN_ASSESTA_MS);
                        CONTROLLA(!(o->qw == rq[i].w && o->qh == rq[i].h && o->qd == rq[i].densita));
                        CONTROLLA(rq[i].densita >= PH_FIN_DENSITA_MIN && rq[i].densita <= PH_FIN_DENSITA_MAX);
                        o->qw = rq[i].w;
                        o->qh = rq[i].h;
                        o->qd = rq[i].densita;
                    }
                }
            }
            /* traduzione dell'input: dentro il video, sempre */
            for (int q = 0; q < 3; q++) {
                PhFinColpo c;
                ph_fin_trova(d, punto(a.w), punto(a.h), &c);
                if (c.zona == PH_FIN_ZONA_CONTENUTO && c.valido)
                    CONTROLLA(c.vx >= 0 && c.vx < c.video_w && c.vy >= 0 && c.vy < c.video_h);
            }
            if (errori) {
                fprintf(stderr, "(invarianti rotti al passo %u, seme %u)\n", passo, s);
                exit(1);
            }
        }
        ph_fin_libera(d);
    }
}

int main(void)
{
    prova_area();
    prova_apertura();
    prova_ciclo_vita();
    prova_solo_verticali();
    prova_gesti();
    prova_ridimensiona();
    prova_ridimensiona_a_cavallo();
    prova_chiusa_durante_gesto();
    prova_host_ridimensionato();
    prova_venti_finestre();
    prova_flusso();
    prova_assestato();
    prova_input();
    prova_ricordi();
    prova_nuove_regole();
    prova_pie_spento();
    prova_densita_automatica();
    prova_solo_densita();
    prova_assestamento_8();
    prova_assestamento_posizione();
    prova_misure_telefono();
    prova_riproporzione();
    prova_specchio_fisso();
    prova_sporge_in_basso();
    prova_cambio_area();
    prova_misura_voluta();
    prova_voluta_salvata();
    prova_compatibilita();
    prova_compatibilita_assestamento();
    prova_punti_minimi();
    prova_solo_verticale_casi();
    prova_a_caso();
    if (errori) {
        fprintf(stderr, "%d controlli falliti\n", errori);
        return 1;
    }
    printf("finestre: tutte le prove riuscite\n");
    return 0;
}
