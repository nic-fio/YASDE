/* Prove di logica di shell-desktop (F-02), SENZA schermo: griglia delle icone,
 * posto dei widget, aggancio a 8 px, posizione dei menu, "Recenti", rettangoli
 * dello sfondo, misure delle immagini ostili, contenuto dei widget, riduzione e
 * scrittura nella cache, CSS. */
#include <cairo.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <gtk/gtk.h>
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "interno.h"
#include "shell/prova_css.h"

void ph_risorse_desktop_register_resource(void);

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

/* ---- Griglia ---- */
static void prova_griglia(void)
{
    /* 1280x800 con widget: (1280-16-240)/92 = 11 colonne; (800-48-16)/96 = 7 righe. */
    PhDskGriglia g = ph_dsk_griglia(1280, 800, true);
    VERIFICA(g.colonne == 11 && g.righe == 7);
    /* Senza widget: (1280-16-16)/92 = 13. */
    VERIFICA(ph_dsk_griglia(1280, 800, false).colonne == 13);
    /* Finestre minuscole: almeno 1x1, mai negativo. */
    g = ph_dsk_griglia(100, 50, true);
    VERIFICA(g.colonne == 1 && g.righe == 1);
    g = ph_dsk_griglia(-5, -5, false);
    VERIFICA(g.colonne == 1 && g.righe == 1);
    /* Tetto contro misure assurde. */
    g = ph_dsk_griglia(2000000000, 2000000000, false);
    VERIFICA(g.colonne == PH_DSK_GRIGLIA_MAX && g.righe == PH_DSK_GRIGLIA_MAX);

    g = ph_dsk_griglia(1280, 800, true);
    PhDskCella c = ph_dsk_cella_al_punto(g, 16 + 92 * 2 + 5, 16 + 96 * 3 + 5);
    VERIFICA(c.c == 2 && c.r == 3);
    c = ph_dsk_cella_al_punto(g, -50, -50); /* fuori a sinistra/in alto: la 0,0 */
    VERIFICA(c.c == 0 && c.r == 0);
    c = ph_dsk_cella_al_punto(g, 99999, 99999); /* ristretta alla griglia */
    VERIFICA(c.c == 10 && c.r == 6);
    c = ph_dsk_cella_al_punto(g, NAN, NAN);
    VERIFICA(c.c == 0 && c.r == 0);
    int x, y;
    ph_dsk_cella_posizione((PhDskCella){2, 3}, &x, &y);
    VERIFICA(x == 16 + 184 && y == 16 + 288);
}

static void prova_cella_libera(void)
{
    PhDskGriglia g = {3, 3};
    PhDskCella out = {-1, -1};
    /* Libera: resta dov'e'. */
    VERIFICA(ph_dsk_cella_libera(g, NULL, 0, (PhDskCella){1, 1}, &out) && out.c == 1 && out.r == 1);
    /* Occupata: la libera piu' vicina (Manhattan); a parita' prima la colonna minore. */
    PhDskCella occ[] = {{1, 1}};
    VERIFICA(ph_dsk_cella_libera(g, occ, 1, (PhDskCella){1, 1}, &out));
    VERIFICA(out.c == 0 && out.r == 1); /* distanza 1: (0,1) viene prima di (1,0) */
    PhDskCella occ2[] = {{1, 1}, {0, 1}, {1, 0}, {2, 1}, {1, 2}};
    VERIFICA(ph_dsk_cella_libera(g, occ2, 5, (PhDskCella){1, 1}, &out));
    VERIFICA(out.c == 0 && out.r == 0); /* le diagonali, distanza 2 */
    /* Griglia piena: nessuna cella, *out intatto. */
    PhDskCella piena[9];
    for (int i = 0; i < 9; i++)
        piena[i] = (PhDskCella){i / 3, i % 3};
    out = (PhDskCella){7, 7};
    VERIFICA(!ph_dsk_cella_libera(g, piena, 9, (PhDskCella){0, 0}, &out) && out.c == 7);
    /* Celle occupate fuori griglia o negative non rompono nulla. */
    PhDskCella fuori[] = {{-1, 0}, {5, 5}, {0, -3}};
    VERIFICA(ph_dsk_cella_libera(g, fuori, 3, (PhDskCella){0, 0}, &out) && out.c == 0 && out.r == 0);
    /* Aggiunta dal launcher: prima libera da 0,0. */
    PhDskCella a[] = {{0, 0}, {0, 1}};
    VERIFICA(ph_dsk_cella_libera(g, a, 2, (PhDskCella){0, 0}, &out) && out.c == 1 && out.r == 0);
}

static void prova_disponi(void)
{
    PhDskGriglia g = {2, 2};
    PhDskCella volute[] = {{0, 0}, {5, 5}, {0, 0}, {1, 1}};
    PhDskCella out[4];
    bool vis[4];
    ph_dsk_disponi(g, volute, 4, out, vis);
    VERIFICA(vis[0] && out[0].c == 0 && out[0].r == 0);   /* resta */
    VERIFICA(vis[3] && out[3].c == 1 && out[3].r == 1);   /* resta */
    /* Fuori griglia (5,5) e doppione (0,0): vanno nelle due libere. */
    VERIFICA(vis[1] && vis[2]);
    VERIFICA(!(out[1].c == out[2].c && out[1].r == out[2].r));
    VERIFICA(!(out[1].c == 0 && out[1].r == 0) && !(out[2].c == 0 && out[2].r == 0));
    /* Piu' icone che celle: l'ultima non si vede. */
    PhDskCella cinque[5] = {{0, 0}, {0, 1}, {1, 0}, {1, 1}, {9, 9}};
    PhDskCella o5[5];
    bool v5[5];
    ph_dsk_disponi(g, cinque, 5, o5, v5);
    VERIFICA(v5[0] && v5[1] && v5[2] && v5[3] && !v5[4]);
}

/* ---- Widget ---- */
static void prova_widget_posto(void)
{
    int d, a;
    const PhDskMisura medio = {250, 100};
    ph_dsk_posto_widget_nuovo(1280, 752, NULL, 0, medio, &d, &a);
    VERIFICA(d == 16 && a == 16);
    /* Sotto a un widget esistente, con 12 px di margine, su un passo di 8 dall'alto. */
    PhDskWidgetPos uno[] = {{16, 16, 250, 100}};
    ph_dsk_posto_widget_nuovo(1280, 752, uno, 1, medio, &d, &a);
    VERIFICA(d == 16 && a == 128);
    /* Un widget alto (meteo Grande 320 x 300): il nuovo non gli si sovrappone. */
    PhDskWidgetPos meteo[] = {{16, 16, 320, 300}};
    ph_dsk_posto_widget_nuovo(1280, 752, meteo, 1, (PhDskMisura){250, 210}, &d, &a);
    VERIFICA(d == 16 && a == 328);
    /* Un widget largo 480 x 300 nella prima colonna, due volte: colonna piena (a 700 px utili),
     * il nuovo va a destra di tutti e due, a 12 px e sul passo di 8. */
    PhDskWidgetPos larghi[] = {{16, 16, 480, 300}, {16, 328, 480, 300}};
    ph_dsk_posto_widget_nuovo(1280, 700, larghi, 2, medio, &d, &a);
    VERIFICA(d == 512 && a == 16);
    /* Due impilati: il terzo va sotto, con 12 px di margine. */
    PhDskWidgetPos due[] = {{16, 16, 250, 100}, {16, 128, 250, 100}};
    ph_dsk_posto_widget_nuovo(1280, 752, due, 2, medio, &d, &a);
    VERIFICA(d == 16 && a == 240);
    /* Area stretta: non ci sta a destra, ma la prima colonna ha ancora posto sotto. */
    ph_dsk_posto_widget_nuovo(300, 752, uno, 1, medio, &d, &a);
    VERIFICA(d == 16 && a == 128);
    /* Nessun posto (area minuscola o troppo larga): a cascata. */
    ph_dsk_posto_widget_nuovo(1280, 100, uno, 1, medio, &d, &a);
    VERIFICA(d == 16 + 24 && a == 16 + 24);
    ph_dsk_posto_widget_nuovo(200, 752, uno, 1, medio, &d, &a);
    VERIFICA(d == 16 + 24 && a == 16 + 24);
    VERIFICA(ph_dsk_larghezza_widget(PH_CFG_WIDGET_PICCOLO) == 200);
    VERIFICA(ph_dsk_larghezza_widget(PH_CFG_WIDGET_MEDIO) == 250);
    VERIFICA(ph_dsk_larghezza_widget(PH_CFG_WIDGET_GRANDE) == 320);
}

static void prova_aggancio(void)
{
    VERIFICA(ph_dsk_aggancia_destra(16, 0) == 16);
    VERIFICA(ph_dsk_aggancia_destra(16, 3) == 16);   /* 13/8 = 1,6 -> 2 -> 16 */
    VERIFICA(ph_dsk_aggancia_destra(16, 6) == 8);    /* 10/8 = 1,25 -> 1 -> 8 */
    VERIFICA(ph_dsk_aggancia_destra(16, 100) == 0);  /* mai negativo */
    VERIFICA(ph_dsk_aggancia_destra(16, -100) == 120); /* 116/8 = 14,5 -> 15 -> 120 */
    VERIFICA(ph_dsk_aggancia_alto(16, 4) == 24);     /* 20/8 = 2,5 -> 3 -> 24 */
    VERIFICA(ph_dsk_aggancia_alto(16, -100) == 0);
    VERIFICA(ph_dsk_aggancia_alto(0, 1e12) == 16384); /* tetto del profilo */
    VERIFICA(ph_dsk_aggancia_alto(16, 0) % PH_DSK_PASSO == 0);
}

static void prova_menu(void)
{
    int x, y;
    ph_dsk_menu_posizione(100, 100, 1280, 800, 200, 110, &x, &y);
    VERIFICA(x == 100 && y == 100);
    ph_dsk_menu_posizione(1270, 790, 1280, 800, PH_DSK_MENU_DESKTOP_L, PH_DSK_MENU_DESKTOP_A, &x, &y);
    VERIFICA(x == 1080 && y == 690);
    ph_dsk_menu_posizione(1270, 790, 1280, 800, PH_DSK_MENU_ICONA_L, PH_DSK_MENU_ICONA_A, &x, &y);
    VERIFICA(x == 1070 && y == 650);
    ph_dsk_menu_posizione(1270, 790, 1280, 800, PH_DSK_MENU_WIDGET_L, PH_DSK_MENU_WIDGET_A, &x, &y);
    VERIFICA(x == 1070 && y == 720);
    ph_dsk_menu_posizione(-5, -5, 100, 100, 200, 200, &x, &y); /* piu' grande dell'area */
    VERIFICA(x == 0 && y == 0);
}

/* ---- Recenti ---- */
static void prova_recenti(void)
{
    VERIFICA(ph_dsk_nome_immagine("a.jpg") && ph_dsk_nome_immagine("A.JPEG") &&
             ph_dsk_nome_immagine("x.PnG") && ph_dsk_nome_immagine("x.webp"));
    VERIFICA(!ph_dsk_nome_immagine("a.gif") && !ph_dsk_nome_immagine("a.mp4") &&
             !ph_dsk_nome_immagine(".nascosta.jpg") && !ph_dsk_nome_immagine("jpg") &&
             !ph_dsk_nome_immagine("a.jpgx") && !ph_dsk_nome_immagine("a/b.jpg") &&
             !ph_dsk_nome_immagine("") && !ph_dsk_nome_immagine(NULL));
    char breve[32];
    ph_dsk_nome_breve("IMG_0412.jpg", breve, sizeof breve);
    VERIFICA_STR(breve, "IMG_0412");
    ph_dsk_nome_breve("senza", breve, sizeof breve);
    VERIFICA_STR(breve, "senza");
    ph_dsk_nome_breve("molto_lungo_nome.png", breve, 5);
    VERIFICA_STR(breve, "molt");

    /* 50 voci: il tetto e' 40, dalla piu' recente. */
    PhDskRecente v[50];
    for (int i = 0; i < 50; i++) {
        v[i].percorso = g_strdup_printf("/c/%02d.jpg", i);
        v[i].nome_schermo = NULL;
        v[i].modificato = 1000 + (i * 7) % 50; /* tutti diversi */
    }
    v[10].modificato = v[11].modificato; /* parita': per percorso */
    size_t n = ph_dsk_recenti_ordina(v, 50, PH_DSK_RECENTI_MAX);
    VERIFICA(n == 40);
    bool ordine = true;
    for (size_t i = 1; i < 50; i++)
        if (v[i - 1].modificato < v[i].modificato)
            ordine = false;
    VERIFICA(ordine);
    VERIFICA(ph_dsk_recenti_ordina(v, 3, 40) == 3);
    for (int i = 0; i < 50; i++)
        g_free(v[i].percorso);
}

/* Elenco enorme ordinato per nome (come lo danno i servizi): si tengono le 40 piu'
 * recenti anche se sono in fondo, con memoria fissa. */
static void prova_recenti_scansione(void)
{
    PhDskRecente v[PH_DSK_RECENTI_MAX];
    size_t n = 0;
    int liberati = 0;
    for (int i = 0; i < 6000; i++) { /* nome crescente, data crescente: le nuove in fondo */
        PhDskRecente r = {g_strdup_printf("/c/%05d.jpg", i), NULL, 5000 + i};
        PhDskRecente scartato;
        if (ph_dsk_recenti_inserisci(v, &n, PH_DSK_RECENTI_MAX, &r, &scartato)) {
            g_free(scartato.percorso);
            liberati++;
        }
    }
    VERIFICA(n == PH_DSK_RECENTI_MAX && liberati == 6000 - PH_DSK_RECENTI_MAX);
    VERIFICA(v[0].modificato == 5000 + 5999 && v[39].modificato == 5000 + 5960);
    /* Dal piu' vecchio al piu' nuovo e in disordine: stesso risultato. */
    for (size_t i = 0; i < n; i++)
        g_free(v[i].percorso);
    n = 0;
    for (int i = 5999; i >= 0; i--) {
        PhDskRecente r = {g_strdup_printf("/c/%05d.jpg", i), NULL, 5000 + (i * 7919) % 6000};
        PhDskRecente scartato;
        if (ph_dsk_recenti_inserisci(v, &n, PH_DSK_RECENTI_MAX, &r, &scartato))
            g_free(scartato.percorso);
    }
    bool ordine = n == PH_DSK_RECENTI_MAX;
    for (size_t i = 1; i < n; i++)
        if (v[i - 1].modificato < v[i].modificato)
            ordine = false;
    VERIFICA(ordine && v[0].modificato == 5000 + 5999 && v[39].modificato == 5000 + 5960);
    for (size_t i = 0; i < n; i++)
        g_free(v[i].percorso);
    /* Tetto zero: tutto scartato. */
    PhDskRecente r = {g_strdup("/x.jpg"), NULL, 1}, sc;
    size_t zero = 0;
    VERIFICA(ph_dsk_recenti_inserisci(v, &zero, 0, &r, &sc) && sc.percorso == r.percorso);
    g_free(r.percorso);
}

/* ---- Sfondo ---- */
static bool vicino(float a, float b)
{
    return fabsf(a - b) < 0.01f;
}

static void prova_sfondo(void)
{
    /* Area 1000x500, immagine 400x400. */
    PhDskRett r = ph_dsk_rett_immagine(PH_CFG_ADATT_RIEMPI, 1000, 500, 400, 400);
    VERIFICA(vicino(r.w, 1000) && vicino(r.h, 1000) && vicino(r.x, 0) && vicino(r.y, -250));
    r = ph_dsk_rett_immagine(PH_CFG_ADATT_ADATTA, 1000, 500, 400, 400);
    VERIFICA(vicino(r.w, 500) && vicino(r.h, 500) && vicino(r.x, 250) && vicino(r.y, 0));
    r = ph_dsk_rett_immagine(PH_CFG_ADATT_ALLUNGA, 1000, 500, 400, 400);
    VERIFICA(vicino(r.w, 1000) && vicino(r.h, 500) && vicino(r.x, 0) && vicino(r.y, 0));
    r = ph_dsk_rett_immagine(PH_CFG_ADATT_CENTRA, 1000, 500, 400, 300);
    VERIFICA(vicino(r.w, 400) && vicino(r.h, 300) && vicino(r.x, 300) && vicino(r.y, 100));
    r = ph_dsk_rett_immagine(PH_CFG_ADATT_RIEMPI, 1000, 500, 0, 400);
    VERIFICA(r.w == 0 && r.h == 0);
    r = ph_dsk_rett_immagine(PH_CFG_ADATT_RIEMPI, 0, 500, 10, 10);
    VERIFICA(r.w == 0);

    float x0, y0, x1, y1;
    ph_dsk_gradiente(PH_CFG_DIR_VERTICALE, 100, 200, &x0, &y0, &x1, &y1); /* 180deg: dall'alto */
    VERIFICA(vicino(x0, 50) && vicino(y0, 0) && vicino(x1, 50) && vicino(y1, 200));
    ph_dsk_gradiente(PH_CFG_DIR_ORIZZONTALE, 100, 200, &x0, &y0, &x1, &y1); /* 90deg: da sinistra */
    VERIFICA(vicino(x0, 0) && vicino(y0, 100) && vicino(x1, 100) && vicino(y1, 100));
    ph_dsk_gradiente(PH_CFG_DIR_DIAGONALE, 100, 100, &x0, &y0, &x1, &y1); /* 135deg: angolo a angolo */
    VERIFICA(vicino(x0, 0) && vicino(y0, 0) && vicino(x1, 100) && vicino(y1, 100));
    ph_dsk_gradiente(PH_CFG_DIR_DIAGONALE_INVERSA, 100, 100, &x0, &y0, &x1, &y1); /* 45deg */
    VERIFICA(vicino(x0, 0) && vicino(y0, 100) && vicino(x1, 100) && vicino(y1, 0));
    VERIFICA(ph_dsk_angolo(PH_CFG_DIR_DIAGONALE) == 135 && ph_dsk_angolo(PH_CFG_DIR_VERTICALE) == 180 &&
             ph_dsk_angolo(PH_CFG_DIR_ORIZZONTALE) == 90 && ph_dsk_angolo(PH_CFG_DIR_DIAGONALE_INVERSA) == 45);
}

/* ---- Immagini ostili ---- */
static size_t png_testa(uint8_t *b, uint32_t w, uint32_t h)
{
    static const uint8_t firma[8] = {0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a};
    memset(b, 0, 64);
    memcpy(b, firma, 8);
    b[11] = 13;
    memcpy(b + 12, "IHDR", 4);
    for (int i = 0; i < 4; i++) {
        b[16 + i] = (uint8_t)(w >> (24 - 8 * i));
        b[20 + i] = (uint8_t)(h >> (24 - 8 * i));
    }
    return 33;
}

static void prova_misura_immagini(void)
{
    uint8_t b[64];
    uint32_t w = 0, h = 0;
    size_t n = png_testa(b, 1920, 1080);
    VERIFICA(ph_dsk_immagine_misura(b, n, &w, &h) && w == 1920 && h == 1080);
    VERIFICA(!ph_dsk_immagine_misura(b, 23, &w, &h)); /* troncata */
    VERIFICA(!ph_dsk_immagine_misura(b, 0, &w, &h));
    VERIFICA(!ph_dsk_immagine_misura(NULL, 10, &w, &h));
    b[13] = 'X'; /* non IHDR */
    VERIFICA(!ph_dsk_immagine_misura(b, n, &w, &h));

    /* JPEG: SOI, APP0 (16 byte), SOF0 con 480x640. */
    uint8_t j[64] = {0xff, 0xd8, 0xff, 0xe0, 0x00, 0x10, 'J', 'F', 'I', 'F', 0, 1, 1, 0, 0, 1, 0, 1, 0, 0,
                     0xff, 0xc0, 0x00, 0x11, 8, 0x02, 0x80, 0x01, 0xe0, 3};
    VERIFICA(ph_dsk_immagine_misura(j, sizeof j, &w, &h) && h == 640 && w == 480);
    VERIFICA(!ph_dsk_immagine_misura(j, 22, &w, &h)); /* SOF troncato */
    VERIFICA(!ph_dsk_immagine_misura(j, 8, &w, &h));  /* il segmento APP0 esce dal buffer */
    uint8_t jm[8] = {0xff, 0xd8, 0xff, 0xe0, 0xff, 0xff, 0, 0}; /* lunghezza enorme */
    VERIFICA(!ph_dsk_immagine_misura(jm, sizeof jm, &w, &h));
    uint8_t jz[8] = {0xff, 0xd8, 0xff, 0xe0, 0x00, 0x00, 0, 0}; /* lunghezza 0: niente ciclo infinito */
    VERIFICA(!ph_dsk_immagine_misura(jz, sizeof jz, &w, &h));

    /* WebP: VP8X 4000x3000 (valori meno uno, 24 bit). */
    uint8_t p[40];
    memset(p, 0, sizeof p);
    memcpy(p, "RIFF", 4);
    memcpy(p + 8, "WEBP", 4);
    memcpy(p + 12, "VP8X", 4);
    p[24] = (3999 & 0xff);
    p[25] = (3999 >> 8) & 0xff;
    p[27] = (2999 & 0xff);
    p[28] = (2999 >> 8) & 0xff;
    VERIFICA(ph_dsk_immagine_misura(p, sizeof p, &w, &h) && w == 4000 && h == 3000);
    VERIFICA(!ph_dsk_immagine_misura(p, 29, &w, &h));
    VERIFICA(!ph_dsk_immagine_misura((const uint8_t *)"GIF89a................................", 36, &w, &h));

    VERIFICA(ph_dsk_immagine_ammessa(1920, 1080));
    VERIFICA(!ph_dsk_immagine_ammessa(0, 10));
    VERIFICA(!ph_dsk_immagine_ammessa(10, 0));
    VERIFICA(!ph_dsk_immagine_ammessa(PH_DSK_IMMAGINE_LATO_MAX + 1, 10));
    VERIFICA(!ph_dsk_immagine_ammessa(16000, 16000)); /* troppi pixel */
    VERIFICA(!ph_dsk_immagine_ammessa(0xffffffffu, 0xffffffffu));

    uint32_t nw, nh;
    ph_dsk_immagine_ridotta(4000, 3000, 1920, 1080, &nw, &nh); /* copre 1920x1080 */
    VERIFICA(nw == 1920 && nh == 1440);
    ph_dsk_immagine_ridotta(800, 600, 1920, 1080, &nw, &nh); /* mai ingrandire */
    VERIFICA(nw == 800 && nh == 600);
    ph_dsk_immagine_ridotta(3000, 4000, 1920, 1080, &nw, &nh);
    VERIFICA(nw == 1920 && nh == 2560);
    ph_dsk_immagine_ridotta(10000, 10, 5, 5, &nw, &nh); /* mai sotto 1 */
    VERIFICA(nw >= 1 && nh >= 1);
    char nome[64];
    ph_dsk_nome_sfondo(12345, nome, sizeof nome);
    VERIFICA(ph_cfg_nome_cache_valido(nome));
}

/* ---- Contenuto dei widget ---- */
static void prova_viste(void)
{
    ph_lingua_imposta(PH_LINGUA_IT);
    PhDskVista v;
    char data[64];
    ph_dsk_data_lunga(1, 23, 11, data, sizeof data);
    VERIFICA_STR(data, "Lunedì 23 novembre");
    ph_dsk_data_lunga(9, 23, 11, data, sizeof data);
    VERIFICA_STR(data, ""); /* fuori intervallo */
    ph_lingua_imposta(PH_LINGUA_EN);
    ph_dsk_data_lunga(1, 23, 11, data, sizeof data);
    VERIFICA_STR(data, "Monday, November 23");
    ph_lingua_imposta(PH_LINGUA_IT);

    ph_dsk_vista_orologio(PH_CFG_WIDGET_PICCOLO, "09:05", "Lunedì 23 novembre", &v);
    VERIFICA_STR(v.testa, "09:05");
    VERIFICA(v.testa_grande && v.sotto[0] == '\0' && v.percento == -1 && v.n_righe == 0);
    ph_dsk_vista_orologio(PH_CFG_WIDGET_GRANDE, "09:05", "Lunedì 23 novembre", &v);
    VERIFICA_STR(v.sotto, "Lunedì 23 novembre");
    VERIFICA(v.n_righe == 0); /* il secondo fuso non e' deciso */

    PhSrvTelefono t = {0};
    PhSrvCollegamento c = {0};
    ph_dsk_vista_stato(PH_CFG_WIDGET_GRANDE, &t, &c, &v); /* senza telefono: niente inventato */
    VERIFICA_STR(v.titolo, "Telefono");
    VERIFICA(v.testa[0] == '\0' && v.percento == -1 && v.n_righe == 0);
    t.noto = true;
    t.batteria = 78;
    t.rete = PH_SRV_RETE_WIFI;
    t.pannello_noto = true;
    t.pannello_acceso = false;
    c.ha_telefono = true;
    c.stato = PH_SRV_BLOCCATO;
    ph_dsk_vista_stato(PH_CFG_WIDGET_GRANDE, &t, &c, &v);
    VERIFICA_STR(v.testa, "Batteria 78%");
    VERIFICA(v.percento == 78 && v.n_righe == 3);
    VERIFICA_STR(v.righe[0].sinistra, "Rete");
    VERIFICA_STR(v.righe[0].destra, "Wi-Fi connesso");
    VERIFICA_STR(v.righe[1].sinistra, "Pannello fisico");
    VERIFICA_STR(v.righe[1].destra, "spento");
    VERIFICA_STR(v.righe[2].sinistra, "Schermo");
    VERIFICA_STR(v.righe[2].destra, "bloccato");
    ph_dsk_vista_stato(PH_CFG_WIDGET_MEDIO, &t, &c, &v);
    VERIFICA(v.n_righe == 1);
    ph_dsk_vista_stato(PH_CFG_WIDGET_PICCOLO, &t, &c, &v);
    VERIFICA(v.n_righe == 0 && v.percento == 78);
    t.batteria = 250; /* dato fuori intervallo: ristretto */
    ph_dsk_vista_stato(PH_CFG_WIDGET_PICCOLO, &t, &c, &v);
    VERIFICA(v.percento == 100);

    PhDskNotifica n[5] = {{"Anna", "Ciao"}, {"Lavoro", "Riunione"}, {"Banca", ""}, {"D", "d"}, {"E", "e"}};
    ph_dsk_vista_notifiche(PH_CFG_WIDGET_GRANDE, n, 0, &v);
    VERIFICA_STR(v.testa, "Nessuna notifica");
    ph_dsk_vista_notifiche(PH_CFG_WIDGET_GRANDE, n, 1, &v);
    VERIFICA_STR(v.testa, "1 nuova");
    ph_dsk_vista_notifiche(PH_CFG_WIDGET_GRANDE, n, 5, &v);
    VERIFICA_STR(v.testa, "5 nuove");
    VERIFICA(v.n_righe == 4);
    VERIFICA_STR(v.righe[0].sinistra, "Anna: Ciao");
    VERIFICA_STR(v.righe[2].sinistra, "Banca"); /* testo vuoto: solo il titolo */
    ph_dsk_vista_notifiche(PH_CFG_WIDGET_MEDIO, n, 5, &v);
    VERIFICA(v.n_righe == 2);
    ph_dsk_vista_notifiche(PH_CFG_WIDGET_PICCOLO, n, 5, &v);
    VERIFICA(v.n_righe == 1);
    char lungo[400];
    memset(lungo, 'x', sizeof lungo - 1);
    lungo[sizeof lungo - 1] = '\0';
    PhDskNotifica nl[1] = {{lungo, lungo}};
    ph_dsk_vista_notifiche(PH_CFG_WIDGET_PICCOLO, nl, 1, &v); /* troncato, senza uscire dal buffer */
    VERIFICA(strlen(v.righe[0].sinistra) < sizeof v.righe[0].sinistra);

    char gb[32];
    ph_dsk_formato_gb(160ull * 1073741824ull, gb, sizeof gb);
    VERIFICA_STR(gb, "160,0");
    ph_dsk_formato_gb(1073741824ull * 3 / 2, gb, sizeof gb);
    VERIFICA_STR(gb, "1,5");
    ph_lingua_imposta(PH_LINGUA_EN);
    ph_dsk_formato_gb(1073741824ull * 3 / 2, gb, sizeof gb);
    VERIFICA_STR(gb, "1.5");
    ph_lingua_imposta(PH_LINGUA_IT);
    ph_dsk_vista_memoria(PH_CFG_WIDGET_MEDIO, true, 256ull << 30, 96ull << 30, NULL, &v);
    VERIFICA_STR(v.testa, "160,0 GB usati");
    VERIFICA_STR(v.sotto, "su 256,0 GB");
    VERIFICA(v.percento == 63 && v.n_righe == 0);
    PhSrvMemoriaDettaglio det = {{38ull << 30, true}, {54ull << 30, true}, {0, false}, {41ull << 30, true}};
    ph_dsk_vista_memoria(PH_CFG_WIDGET_GRANDE, true, 256ull << 30, 96ull << 30, &det, &v);
    VERIFICA(v.n_righe == 3); /* l'audio non e' leggibile: non si inventa */
    VERIFICA_STR(v.righe[0].sinistra, "Immagini");
    VERIFICA_STR(v.righe[0].destra, "38,0 GB");
    VERIFICA_STR(v.righe[1].sinistra, "Video");
    VERIFICA_STR(v.righe[2].sinistra, "App");
    ph_dsk_vista_memoria(PH_CFG_WIDGET_MEDIO, true, 256ull << 30, 96ull << 30, &det, &v);
    VERIFICA(v.n_righe == 1);
    ph_dsk_vista_memoria(PH_CFG_WIDGET_PICCOLO, true, 256ull << 30, 96ull << 30, &det, &v);
    VERIFICA(v.n_righe == 0);
    ph_dsk_vista_memoria(PH_CFG_WIDGET_MEDIO, false, 0, 0, NULL, &v);
    VERIFICA(v.testa[0] == '\0' && v.percento == -1);
    ph_dsk_vista_memoria(PH_CFG_WIDGET_MEDIO, true, 100, 500, NULL, &v); /* libera > totale: dato ostile */
    VERIFICA(v.percento == 0);
}

static void prova_calendario(void)
{
    ph_lingua_imposta(PH_LINGUA_IT);
    const int fuso = 3600; /* UTC+1 */
    /* "Adesso" = 2026-11-23 10:00 UTC = 11:00 locali. */
    const uint64_t adesso = 1795428000000ull;
    const uint64_t ora = 3600ull * 1000;
    const uint64_t giorno = 86400ull * 1000;
    uint64_t mezzanotte_utc = (adesso / giorno) * giorno;
    PhDskEvento ev[] = {
        {"passato", adesso - 3 * ora, adesso - ora, false},          /* finito */
        {"in corso", adesso - ora, adesso + ora, false},             /* in corso */
        {"dopo", adesso + 2 * ora, adesso + 3 * ora, false},         /* oggi */
        {"domani", adesso + 20 * ora, adesso + 21 * ora, false},     /* 07:00 di domani */
        {"tutto il giorno", mezzanotte_utc, mezzanotte_utc + giorno, true},
        {"ieri tutto", mezzanotte_utc - giorno, mezzanotte_utc, true},
    };
    size_t idx[8];
    size_t k = ph_dsk_eventi_di_oggi(ev, 6, adesso, fuso, 8, idx);
    VERIFICA(k == 3 && idx[0] == 1 && idx[1] == 2 && idx[2] == 4);
    k = ph_dsk_eventi_di_oggi(ev, 6, adesso, fuso, 2, idx);
    VERIFICA(k == 2);
    VERIFICA(ph_dsk_eventi_di_oggi(NULL, 0, adesso, fuso, 8, idx) == 0);
    char o[8];
    ph_dsk_ora_evento(adesso, fuso, o, sizeof o);
    VERIFICA_STR(o, "11:00");
    ph_dsk_ora_evento(adesso, -5 * 3600, o, sizeof o);
    VERIFICA_STR(o, "05:00");
    ph_dsk_ora_evento(0, -3600, o, sizeof o); /* prima del 1970 locale: 23:00 */
    VERIFICA_STR(o, "23:00");

    PhDskVista v;
    ph_dsk_vista_calendario(PH_CFG_WIDGET_MEDIO, true, ev, 6, adesso, fuso, "Lunedì 23 novembre", &v);
    VERIFICA_STR(v.titolo, "Calendario");
    VERIFICA_STR(v.testa, "Oggi");
    VERIFICA_STR(v.sotto, "Lunedì 23 novembre");
    VERIFICA(v.n_righe == 2);
    VERIFICA_STR(v.righe[0].sinistra, "in corso");
    VERIFICA_STR(v.righe[0].destra, "10:00");
    ph_dsk_vista_calendario(PH_CFG_WIDGET_GRANDE, true, ev, 6, adesso, fuso, "x", &v);
    VERIFICA(v.n_righe == 3);
    VERIFICA_STR(v.righe[2].sinistra, "tutto il giorno");
    VERIFICA_STR(v.righe[2].destra, ""); /* tutto il giorno: niente ora, niente fuso */
    ph_dsk_vista_calendario(PH_CFG_WIDGET_GRANDE, false, NULL, 0, adesso, fuso, "x", &v);
    VERIFICA(v.n_righe == 0 && v.testa[0] != '\0'); /* dati non disponibili: non si inventa */
}

/* ---- Riduzione e cache ---- */
static GBytes *png_cairo(int w, int h)
{
    cairo_surface_t *s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
    cairo_t *cr = cairo_create(s);
    cairo_set_source_rgb(cr, 0.2, 0.5, 0.8);
    cairo_paint(cr);
    cairo_destroy(cr);
    char *percorso = g_build_filename(g_get_tmp_dir(), "ph-desktop-prova-src.png", NULL);
    cairo_surface_write_to_png(s, percorso);
    cairo_surface_destroy(s);
    gchar *dati = NULL;
    gsize len = 0;
    GBytes *b = NULL;
    if (g_file_get_contents(percorso, &dati, &len, NULL))
        b = g_bytes_new_take(dati, len);
    g_remove(percorso);
    g_free(percorso);
    return b;
}

/* JPEG 40x20 con un segmento EXIF che dice "orientamento 6" (ruotare di 90 gradi). */
static GBytes *jpeg_orientato(int orientamento)
{
    GdkPixbuf *pb = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8, 40, 20);
    gdk_pixbuf_fill(pb, 0x336699ff);
    gchar *jpg = NULL;
    gsize n = 0;
    if (!gdk_pixbuf_save_to_buffer(pb, &jpg, &n, "jpeg", NULL, NULL) || n < 4) {
        g_object_unref(pb);
        return NULL;
    }
    g_object_unref(pb);
    const uint8_t exif[] = {0xff, 0xe1, 0x00, 0x22, 'E', 'x', 'i', 'f', 0, 0,
                            'M', 'M', 0, 42, 0, 0, 0, 8,                 /* TIFF, IFD a 8 */
                            0, 1,                                        /* una voce */
                            0x01, 0x12, 0, 3, 0, 0, 0, 1, 0, (uint8_t)orientamento, 0, 0, /* orientamento */
                            0, 0, 0, 0};                                 /* niente altro IFD */
    GByteArray *a = g_byte_array_new();
    g_byte_array_append(a, (const guint8 *)jpg, 2); /* SOI */
    g_byte_array_append(a, exif, sizeof exif);
    g_byte_array_append(a, (const guint8 *)jpg + 2, (guint)(n - 2));
    g_free(jpg);
    return g_byte_array_free_to_bytes(a);
}

static void prova_cache(void)
{
    char *dir = g_dir_make_tmp("ph-desktop-XXXXXX", NULL);
    VERIFICA(dir != NULL);
    if (dir == NULL)
        return;
    g_setenv("XDG_CACHE_HOME", dir, TRUE);
    PhErrore e = {{0}};
    char nome[PH_CFG_LUN_FILE_CACHE] = "";

    GBytes *png = png_cairo(400, 300);
    VERIFICA(png != NULL);
    if (png != NULL) {
        /* Desktop 200x100: copre riducendo a 200x150, senza ingrandire. */
        VERIFICA(ph_dsk_immagine_prepara(png, 200, 100, NULL, nome, sizeof nome, &e) == 0);
        VERIFICA(ph_cfg_nome_cache_valido(nome));
        GdkTexture *t = ph_dsk_immagine_da_cache(nome);
        VERIFICA(t != NULL);
        if (t != NULL) {
            VERIFICA(gdk_texture_get_width(t) == 200 && gdk_texture_get_height(t) == 150);
            g_object_unref(t);
        }
        /* Il nome nuovo e' diverso dal precedente. */
        char nome2[PH_CFG_LUN_FILE_CACHE];
        VERIFICA(ph_dsk_immagine_prepara(png, 5000, 5000, nome, nome2, sizeof nome2, &e) == 0);
        VERIFICA(strcmp(nome, nome2) != 0);
        GdkTexture *t2 = ph_dsk_immagine_da_cache(nome2);
        VERIFICA(t2 != NULL && gdk_texture_get_width(t2) == 400); /* mai ingrandita */
        if (t2 != NULL)
            g_object_unref(t2);
        ph_dsk_immagine_togli(nome);
        VERIFICA(ph_dsk_immagine_da_cache(nome) == NULL);
        ph_dsk_immagine_togli(nome2);
        g_bytes_unref(png);
    }

    /* Foto verticale del telefono: EXIF 6, 40x20 -> 20x40 (la miniatura arriva dritta,
     * lo sfondo deve essere dritto anche lui). */
    GBytes *fv = jpeg_orientato(6);
    VERIFICA(fv != NULL);
    if (fv != NULL) {
        char nr[PH_CFG_LUN_FILE_CACHE];
        VERIFICA(ph_dsk_immagine_prepara(fv, 1000, 1000, NULL, nr, sizeof nr, &e) == 0);
        GdkTexture *tr = ph_dsk_immagine_da_cache(nr);
        VERIFICA(tr != NULL && gdk_texture_get_width(tr) == 20 && gdk_texture_get_height(tr) == 40);
        if (tr != NULL)
            g_object_unref(tr);
        ph_dsk_immagine_togli(nr);
        g_bytes_unref(fv);
    }
    fv = jpeg_orientato(1); /* senza rotazione resta 40x20 */
    if (fv != NULL) {
        char nr[PH_CFG_LUN_FILE_CACHE];
        VERIFICA(ph_dsk_immagine_prepara(fv, 1000, 1000, NULL, nr, sizeof nr, &e) == 0);
        GdkTexture *tr = ph_dsk_immagine_da_cache(nr);
        VERIFICA(tr != NULL && gdk_texture_get_width(tr) == 40 && gdk_texture_get_height(tr) == 20);
        if (tr != NULL)
            g_object_unref(tr);
        ph_dsk_immagine_togli(nr);
        g_bytes_unref(fv);
    }
    fv = jpeg_orientato(8); /* 270 gradi, con riduzione: l'area 10x10 resta coperta */
    if (fv != NULL) {
        char nr[PH_CFG_LUN_FILE_CACHE];
        VERIFICA(ph_dsk_immagine_prepara(fv, 10, 10, NULL, nr, sizeof nr, &e) == 0);
        GdkTexture *tr = ph_dsk_immagine_da_cache(nr);
        VERIFICA(tr != NULL && gdk_texture_get_width(tr) == 10 && gdk_texture_get_height(tr) == 20);
        if (tr != NULL)
            g_object_unref(tr);
        ph_dsk_immagine_togli(nr);
        g_bytes_unref(fv);
    }
    /* Valore assurdo dell'orientamento (99): ignorato, nessun errore. */
    fv = jpeg_orientato(99);
    if (fv != NULL) {
        char nr[PH_CFG_LUN_FILE_CACHE];
        VERIFICA(ph_dsk_immagine_prepara(fv, 1000, 1000, NULL, nr, sizeof nr, &e) == 0);
        ph_dsk_immagine_togli(nr);
        g_bytes_unref(fv);
    }
    /* EXIF troncato: nessun crash. */
    fv = jpeg_orientato(6);
    if (fv != NULL) {
        gsize nn = 0;
        const uint8_t *dd = g_bytes_get_data(fv, &nn);
        for (gsize cut = 4; cut < 60 && cut < nn; cut += 7) {
            GBytes *tr = g_bytes_new(dd, cut);
            char nr[PH_CFG_LUN_FILE_CACHE];
            if (ph_dsk_immagine_prepara(tr, 1000, 1000, NULL, nr, sizeof nr, &e) == 0)
                ph_dsk_immagine_togli(nr);
            g_bytes_unref(tr);
        }
        g_bytes_unref(fv);
    }

    /* Dati ostili: nessuna scrittura, errore. */
    uint8_t enorme[64];
    png_testa(enorme, 40000, 40000);
    GBytes *b = g_bytes_new(enorme, 33);
    VERIFICA(ph_dsk_immagine_prepara(b, 100, 100, NULL, nome, sizeof nome, &e) == -1);
    g_bytes_unref(b);
    b = g_bytes_new("non e' un'immagine", 18);
    VERIFICA(ph_dsk_immagine_prepara(b, 100, 100, NULL, nome, sizeof nome, &e) == -1);
    g_bytes_unref(b);
    png_testa(enorme, 100, 100); /* intestazione buona, dati assenti: la decodifica fallisce */
    b = g_bytes_new(enorme, 33);
    VERIFICA(ph_dsk_immagine_prepara(b, 100, 100, NULL, nome, sizeof nome, &e) == -1);
    g_bytes_unref(b);
    /* Un file della cache che non c'e', o con nome non valido. */
    VERIFICA(ph_dsk_immagine_da_cache("non-esiste.png") == NULL);
    VERIFICA(ph_dsk_immagine_da_cache("../fuori.png") == NULL);
    VERIFICA(ph_dsk_immagine_da_cache(NULL) == NULL);
    ph_dsk_immagine_togli("../fuori.png"); /* non fa nulla */
    {
        /* Non e' un nostro file ("sfondo-*.png"): resta davvero. */
        char altro[PH_CFG_LUN_PERCORSO];
        VERIFICA(ph_cfg_percorso_cache("altro.png", altro, sizeof altro, &e) == 0);
        VERIFICA(ph_cfg_scrivi_file(altro, "x", 1, &e) == 0);
        ph_dsk_immagine_togli("altro.png");
        VERIFICA(g_file_test(altro, G_FILE_TEST_EXISTS));
        g_remove(altro);
    }

    char *sotto = g_build_filename(dir, "YASDE", NULL);
    g_rmdir(sotto); /* i file di prova sono gia' stati tolti */
    g_free(sotto);
    g_rmdir(dir);
    g_free(dir);
}

static void prova_catalogo(void)
{
    /* Il catalogo ha almeno i cinque widget iniziali (poi ne sono stati aggiunti altri). */
    VERIFICA(PH_DSK_CATALOGO_N >= 5);
    /* «Trova il telefono» non e' offerto: ne' nel catalogo ne' tra i nomi noti. */
    for (size_t i = 0; i < PH_DSK_CATALOGO_N; i++)
        VERIFICA(PH_DSK_CATALOGO[i].tipo != PH_CFG_WIDGET_TROVA);
    VERIFICA(ph_dsk_widget_nome(PH_CFG_WIDGET_TROVA) == NULL);
    VERIFICA(ph_dsk_widget_nome(PH_CFG_WIDGET_INTERRUTTORI) != NULL);
    {
        /* Un profilo con un widget TROVA si carica, resta com'e' e gli altri si vedono. */
        const char *testo =
            "[[widget]]\ntipo = \"trova\"\ndestra = 16\nalto = 16\ndimensione = \"medio\"\n"
            "[[widget]]\ntipo = \"orologio\"\ndestra = 300\nalto = 16\ndimensione = \"piccolo\"\n";
        PhCfgTelefono *p = ph_cfg_telefono_nuovo();
        PhErrore e = {{0}};
        VERIFICA(p != NULL && ph_cfg_telefono_da_testo(testo, strlen(testo), p, &e) == 0);
        if (p != NULL) {
            VERIFICA(p->n_widget == 2 && p->widget[0].tipo == PH_CFG_WIDGET_TROVA);
            size_t mostrati = 0;
            for (size_t i = 0; i < p->n_widget; i++)
                if (ph_dsk_widget_nome(p->widget[i].tipo) != NULL)
                    mostrati++; /* come ph_dsk_widget_ricostruisci: gli altri si disegnano */
            VERIFICA(mostrati == 1);
            ph_cfg_telefono_libera(p);
        }
    }
    VERIFICA(ph_dsk_widget_nome(PH_CFG_WIDGET_OROLOGIO) != NULL);
    VERIFICA(ph_dsk_widget_nome(PH_CFG_WIDGET_CALENDARIO) != NULL);
    VERIFICA(ph_dsk_widget_nome(PH_CFG_WIDGET_STATO) != NULL);
    VERIFICA(ph_dsk_widget_nome(PH_CFG_WIDGET_NOTIFICHE) != NULL);
    VERIFICA(ph_dsk_widget_nome(PH_CFG_WIDGET_MEMORIA) != NULL);
    VERIFICA(ph_dsk_widget_nome(PH_CFG_WIDGET_NOTE) == NULL);
    VERIFICA(ph_dsk_widget_nome(PH_CFG_WIDGET_METEO) != NULL); /* il meteo e' nel catalogo (§18) */
    VERIFICA(ph_dsk_widget_nome(PH_CFG_WIDGET_RIPRODUZIONE) == NULL);
}

/* ---- Forma e misura libera (PIANO.md 8.1) ---- */
static void prova_forme(void)
{
    /* Forma per tipo e stile. */
    VERIFICA(ph_dsk_forma(PH_CFG_WIDGET_OROLOGIO, PH_DSK_OROLOGIO_CLASSICO) == PH_DSK_FORMA_TONDO);
    VERIFICA(ph_dsk_forma(PH_CFG_WIDGET_OROLOGIO, PH_DSK_OROLOGIO_ESSENZIALE) == PH_DSK_FORMA_TONDO);
    VERIFICA(ph_dsk_forma(PH_CFG_WIDGET_OROLOGIO, PH_DSK_OROLOGIO_DIGITALE) == PH_DSK_FORMA_MATTONELLA);
    VERIFICA(ph_dsk_forma(PH_CFG_WIDGET_OROLOGIO, PH_DSK_OROLOGIO_PALETTE) == PH_DSK_FORMA_MATTONELLA);
    VERIFICA(ph_dsk_forma(PH_CFG_WIDGET_OROLOGIO, PH_DSK_OROLOGIO_TESTUALE) == PH_DSK_FORMA_MATTONELLA);
    VERIFICA(ph_dsk_forma(PH_CFG_WIDGET_OROLOGIO, PH_DSK_OROLOGIO_DOPPIO_FUSO) == PH_DSK_FORMA_MATTONELLA);
    /* Stile fuori elenco (profilo rovinato): vale il digitale, quindi la mattonella. */
    VERIFICA(ph_dsk_forma(PH_CFG_WIDGET_OROLOGIO, 99) == PH_DSK_FORMA_MATTONELLA);
    VERIFICA(ph_dsk_forma(PH_CFG_WIDGET_BATTERIA, 0) == PH_DSK_FORMA_TONDO);
    VERIFICA(ph_dsk_forma(PH_CFG_WIDGET_SVEGLIA, 0) == PH_DSK_FORMA_CAPSULA);
    VERIFICA(ph_dsk_forma(PH_CFG_WIDGET_INTERRUTTORI, 0) == PH_DSK_FORMA_CAPSULA);
    static const PhCfgWidgetTipo tutti[] = {
        PH_CFG_WIDGET_OROLOGIO, PH_CFG_WIDGET_STATO, PH_CFG_WIDGET_NOTIFICHE, PH_CFG_WIDGET_MEMORIA,
        PH_CFG_WIDGET_CALENDARIO, PH_CFG_WIDGET_NOTE, PH_CFG_WIDGET_METEO, PH_CFG_WIDGET_RIPRODUZIONE,
        PH_CFG_WIDGET_CALENDARIO_MESE, PH_CFG_WIDGET_SVEGLIA, PH_CFG_WIDGET_BATTERIA,
        PH_CFG_WIDGET_INTERRUTTORI, PH_CFG_WIDGET_TROVA, PH_CFG_WIDGET_APP_FREQUENTI,
        PH_CFG_WIDGET_TRASFERIMENTI};
    for (size_t i = 0; i < sizeof tutti / sizeof tutti[0]; i++) {
        PhCfgWidgetTipo t = tutti[i];
        bool rotondo = t == PH_CFG_WIDGET_BATTERIA || t == PH_CFG_WIDGET_SVEGLIA ||
                       t == PH_CFG_WIDGET_INTERRUTTORI;
        VERIFICA(rotondo || t == PH_CFG_WIDGET_OROLOGIO || ph_dsk_forma(t, 0) == PH_DSK_FORMA_MATTONELLA);
    }

    /* Raggio: mattonella 28 (ma mai oltre meta' del lato corto), tondo lato/2, capsula altezza/2. */
    VERIFICA(ph_dsk_raggio(PH_DSK_FORMA_MATTONELLA, 250, 100) == 28);
    VERIFICA(ph_dsk_raggio(PH_DSK_FORMA_MATTONELLA, 40, 400) == 20);
    VERIFICA(ph_dsk_raggio(PH_DSK_FORMA_TONDO, 184, 184) == 92);
    VERIFICA(ph_dsk_raggio(PH_DSK_FORMA_TONDO, 185, 185) == 92);
    VERIFICA(ph_dsk_raggio(PH_DSK_FORMA_CAPSULA, 250, 80) == 40);
    VERIFICA(ph_dsk_raggio(PH_DSK_FORMA_CAPSULA, 250, 72) == 36);
    VERIFICA(ph_dsk_raggio(PH_DSK_FORMA_TONDO, -5, -5) == 0 && ph_dsk_raggio(PH_DSK_FORMA_CAPSULA, 10, -5) == 0);

    int po, pv;
    ph_dsk_imbottitura(PH_DSK_FORMA_MATTONELLA, &po, &pv);
    VERIFICA(po == 20 && pv == 16);
    ph_dsk_imbottitura(PH_DSK_FORMA_TONDO, &po, &pv);
    VERIFICA(po == 12 && pv == 12);
    ph_dsk_imbottitura(PH_DSK_FORMA_CAPSULA, &po, &pv);
    VERIFICA(po == 24 && pv == 10);

    /* Misure equivalenti: stanno nei limiti, sono quadrate per il tondo, il minimo e' il Piccolo,
     * e dalla misura si ritrova il livello (cosi' un profilo senza misure non perde nulla). */
    for (size_t i = 0; i < sizeof tutti / sizeof tutti[0]; i++)
        for (unsigned st = 0; st < ph_dsk_stili_n(tutti[i]); st++) {
            PhCfgWidgetTipo t = tutti[i];
            PhDskLimiti lim = ph_dsk_misura_limiti(t, st);
            PhDskMisura mp = ph_dsk_misura_equivalente(t, st, PH_CFG_WIDGET_PICCOLO);
            VERIFICA(lim.min.larghezza == mp.larghezza && lim.min.altezza == mp.altezza);
            VERIFICA(lim.max.larghezza > lim.min.larghezza && lim.max.altezza >= lim.min.altezza);
            VERIFICA(lim.min.larghezza >= 48 && lim.max.larghezza <= 2048); /* ammessi dal profilo */
            for (int dd = 0; dd < 3; dd++) {
                PhDskMisura m = ph_dsk_misura_equivalente(t, st, (PhCfgWidgetDim)dd);
                VERIFICA(m.larghezza <= lim.max.larghezza && m.altezza <= lim.max.altezza);
                VERIFICA(m.larghezza >= lim.min.larghezza && m.altezza >= lim.min.altezza);
                VERIFICA(ph_dsk_livello(t, st, m) == (PhCfgWidgetDim)dd);
                PhDskMisura v = ph_dsk_misura_vincola(t, st, m);
                VERIFICA(v.larghezza == m.larghezza && v.altezza == m.altezza);
                if (ph_dsk_forma(t, st) == PH_DSK_FORMA_TONDO)
                    VERIFICA(m.larghezza == m.altezza);
                PhDskMisura da = ph_dsk_misura_da_profilo(t, st, (PhCfgWidgetDim)dd, 0, 0);
                VERIFICA(da.larghezza == m.larghezza && da.altezza == m.altezza);
            }
        }
    /* Le vecchie larghezze restano le larghezze dei mattonelle di oggi. */
    for (int dd = 0; dd < 3; dd++)
        VERIFICA(ph_dsk_misura_equivalente(PH_CFG_WIDGET_NOTIFICHE, 0, (PhCfgWidgetDim)dd).larghezza ==
                 ph_dsk_larghezza_widget((PhCfgWidgetDim)dd));
    /* Il quadrante dell'orologio analogico sta nel tondo (larghezza interna = lato - 2 * 12). */
    for (int dd = 0; dd < 3; dd++)
        for (unsigned st = PH_DSK_OROLOGIO_CLASSICO; st <= PH_DSK_OROLOGIO_ESSENZIALE; st++) {
            PhDskMisura m = ph_dsk_misura_equivalente(PH_CFG_WIDGET_OROLOGIO, st, (PhCfgWidgetDim)dd);
            VERIFICA(ph_dsk_quadrante_diametro(st, (PhCfgWidgetDim)dd) <= m.larghezza - 24);
            VERIFICA(ph_dsk_orologio_altezza(st, (PhCfgWidgetDim)dd) <= m.altezza - 24);
        }

    /* Dal profilo: salvate e valide, si usano (dentro i limiti); 0 su un lato = equivalenti. */
    PhDskMisura m = ph_dsk_misura_da_profilo(PH_CFG_WIDGET_NOTIFICHE, 0, PH_CFG_WIDGET_PICCOLO, 300, 200);
    VERIFICA(m.larghezza == 300 && m.altezza == 200);
    m = ph_dsk_misura_da_profilo(PH_CFG_WIDGET_NOTIFICHE, 0, PH_CFG_WIDGET_GRANDE, 300, 0);
    VERIFICA(m.larghezza == 320 && m.altezza == 168);
    m = ph_dsk_misura_da_profilo(PH_CFG_WIDGET_NOTIFICHE, 0, PH_CFG_WIDGET_GRANDE, 0, 200);
    VERIFICA(m.larghezza == 320 && m.altezza == 168);
    m = ph_dsk_misura_da_profilo(PH_CFG_WIDGET_NOTIFICHE, 0, PH_CFG_WIDGET_MEDIO, 48, 48); /* sotto il minimo */
    VERIFICA(m.larghezza == 200 && m.altezza == 104);
    m = ph_dsk_misura_da_profilo(PH_CFG_WIDGET_NOTIFICHE, 0, PH_CFG_WIDGET_MEDIO, 2048, 2048); /* sopra il massimo */
    VERIFICA(m.larghezza == 480 && m.altezza == 640);
    m = ph_dsk_misura_da_profilo(PH_CFG_WIDGET_NOTIFICHE, 0, PH_CFG_WIDGET_MEDIO, -3, 500);
    VERIFICA(m.larghezza == 250 && m.altezza == 128);
    m = ph_dsk_misura_da_profilo(PH_CFG_WIDGET_BATTERIA, 0, PH_CFG_WIDGET_MEDIO, 300, 200); /* tondo: il lato corto */
    VERIFICA(m.larghezza == 200 && m.altezza == 200);
    m = ph_dsk_misura_da_profilo(PH_CFG_WIDGET_BATTERIA, 0, PH_CFG_WIDGET_MEDIO, 500, 500);
    VERIFICA(m.larghezza == 320 && m.altezza == 320);
    m = ph_dsk_misura_da_profilo(PH_CFG_WIDGET_SVEGLIA, 0, PH_CFG_WIDGET_MEDIO, 300, 500); /* capsula bassa */
    VERIFICA(m.larghezza == 300 && m.altezza == 128);
    /* Una misura salvata con un altro stile (tondo -> mattonella) si adatta ai limiti del nuovo. */
    m = ph_dsk_misura_da_profilo(PH_CFG_WIDGET_OROLOGIO, PH_DSK_OROLOGIO_CLASSICO, PH_CFG_WIDGET_MEDIO, 250, 100);
    VERIFICA(m.larghezza == 144 && m.altezza == 144);
    /* Il campo del profilo c'e' e vale 0 di fabbrica. */
    PhCfgWidget cw;
    memset(&cw, 0, sizeof cw);
    VERIFICA(cw.larghezza == 0 && cw.altezza == 0);

    /* Livello dalla misura: serve tutta la larghezza E tutta l'altezza dell'equivalente. */
    VERIFICA(ph_dsk_livello(PH_CFG_WIDGET_OROLOGIO, 0, (PhDskMisura){249, 400}) == PH_CFG_WIDGET_PICCOLO);
    VERIFICA(ph_dsk_livello(PH_CFG_WIDGET_OROLOGIO, 0, (PhDskMisura){250, 100}) == PH_CFG_WIDGET_MEDIO);
    VERIFICA(ph_dsk_livello(PH_CFG_WIDGET_OROLOGIO, 0, (PhDskMisura){250, 99}) == PH_CFG_WIDGET_PICCOLO);
    VERIFICA(ph_dsk_livello(PH_CFG_WIDGET_OROLOGIO, 0, (PhDskMisura){319, 400}) == PH_CFG_WIDGET_MEDIO);
    VERIFICA(ph_dsk_livello(PH_CFG_WIDGET_OROLOGIO, 0, (PhDskMisura){480, 100}) == PH_CFG_WIDGET_GRANDE);
    VERIFICA(ph_dsk_livello(PH_CFG_WIDGET_OROLOGIO, 0, (PhDskMisura){1, 1}) == PH_CFG_WIDGET_PICCOLO);
    VERIFICA(ph_dsk_livello(PH_CFG_WIDGET_BATTERIA, 0, (PhDskMisura){200, 200}) == PH_CFG_WIDGET_MEDIO);
    VERIFICA(ph_dsk_livello(PH_CFG_WIDGET_BATTERIA, 0, (PhDskMisura){216, 216}) == PH_CFG_WIDGET_GRANDE);

    /* Ridimensionamento: scatti di 8 px, l'angolo in alto a sinistra fermo (destra cala). */
    PhDskMisura n;
    int destra;
    const PhDskMisura base = {250, 100}; /* orologio digitale, Medio */
    VERIFICA(ph_dsk_ridimensiona(PH_CFG_WIDGET_OROLOGIO, 0, base, 100, 50, 17, 0, 700, &n, &destra));
    VERIFICA(n.larghezza == 266 && n.altezza == 100 && destra == 84);
    VERIFICA(ph_dsk_ridimensiona(PH_CFG_WIDGET_OROLOGIO, 0, base, 100, 50, 11, 30, 700, &n, &destra));
    VERIFICA(n.larghezza == 258 && n.altezza == 132 && destra == 92); /* 11/8 -> 1 scatto; 30/8 -> 4 scatti */
    VERIFICA(ph_dsk_ridimensiona(PH_CFG_WIDGET_OROLOGIO, 0, base, 100, 50, -24, -8, 700, &n, &destra));
    VERIFICA(n.larghezza == 226 && n.altezza == 92 && destra == 124); /* restringe: destra cresce */
    /* Meno di mezzo scatto: nessun cambiamento. */
    VERIFICA(!ph_dsk_ridimensiona(PH_CFG_WIDGET_OROLOGIO, 0, base, 100, 50, 3, -3, 700, &n, &destra));
    VERIFICA(n.larghezza == 250 && n.altezza == 100 && destra == 100);
    /* Minimo del tipo (200 x 80 per l'orologio digitale), anche con spostamenti enormi. */
    VERIFICA(ph_dsk_ridimensiona(PH_CFG_WIDGET_OROLOGIO, 0, base, 100, 50, -5000, -5000, 700, &n, &destra));
    VERIFICA(n.larghezza == 200 && n.altezza == 80 && destra == 150);
    /* Massimo del tipo (480 x 640), con spazio a volonta'. */
    VERIFICA(ph_dsk_ridimensiona(PH_CFG_WIDGET_OROLOGIO, 0, base, 5000, 0, 5000, 5000, 5000, &n, &destra));
    VERIFICA(n.larghezza == 480 && n.altezza == 640 && destra == 4770);
    /* Non esce dall'area: a destra (destra0 + larghezza e' il tetto: destra non scende sotto 0)... */
    VERIFICA(ph_dsk_ridimensiona(PH_CFG_WIDGET_OROLOGIO, 0, base, 16, 50, 400, 0, 700, &n, &destra));
    VERIFICA(n.larghezza == 266 && destra == 0);
    /* ...e in basso (alto0 + altezza <= altezza utile). */
    VERIFICA(!ph_dsk_ridimensiona(PH_CFG_WIDGET_OROLOGIO, 0, base, 100, 600, 0, 400, 700, &n, &destra));
    VERIFICA(n.altezza == 100); /* sotto c'e' posto solo per 100 px: resta com'e' */
    VERIFICA(ph_dsk_ridimensiona(PH_CFG_WIDGET_NOTIFICHE, 0, (PhDskMisura){250, 128}, 100, 300, 0, 400, 700, &n, &destra));
    VERIFICA(n.altezza == 400 && 300 + n.altezza <= 700);
    /* Un widget gia' oltre l'area non si restringe da solo (nemmeno con un passo nullo in avanti)... */
    VERIFICA(!ph_dsk_ridimensiona(PH_CFG_WIDGET_NOTIFICHE, 0, (PhDskMisura){250, 400}, 100, 500, 0, 0, 700, &n, &destra));
    VERIFICA(n.altezza == 400);
    /* ...ma non cresce ancora. */
    VERIFICA(!ph_dsk_ridimensiona(PH_CFG_WIDGET_NOTIFICHE, 0, (PhDskMisura){250, 400}, 100, 500, 0, 80, 700, &n, &destra));
    VERIFICA(n.altezza == 400);
    /* Area minuscola: il minimo del tipo vince. */
    VERIFICA(!ph_dsk_ridimensiona(PH_CFG_WIDGET_NOTIFICHE, 0, (PhDskMisura){250, 128}, 0, 0, 0, 0, 50, &n, &destra));
    VERIFICA(n.altezza == 128 && n.larghezza == 250);
    ph_dsk_ridimensiona(PH_CFG_WIDGET_NOTIFICHE, 0, (PhDskMisura){250, 128}, 0, 0, 0, -800, 50, &n, &destra);
    VERIFICA(n.altezza == 104); /* il minimo del tipo, non meno, qualunque sia l'area */
    /* Il tondo resta tondo con i due assi diversi (media dei due spostamenti, a scatti). */
    VERIFICA(ph_dsk_ridimensiona(PH_CFG_WIDGET_BATTERIA, 0, (PhDskMisura){176, 176}, 100, 50, 40, 0, 700, &n, &destra));
    VERIFICA(n.larghezza == n.altezza && n.larghezza == 200 && destra == 76); /* 20 -> 24 */
    VERIFICA(ph_dsk_ridimensiona(PH_CFG_WIDGET_BATTERIA, 0, (PhDskMisura){176, 176}, 100, 50, -8, -64, 700, &n, &destra));
    VERIFICA(n.larghezza == n.altezza && n.larghezza == 144 && destra == 132); /* -36 -> -32 */
    VERIFICA(ph_dsk_ridimensiona(PH_CFG_WIDGET_BATTERIA, 0, (PhDskMisura){176, 176}, 100, 50, -900, 30, 700, &n, &destra));
    VERIFICA(n.larghezza == 136 && n.altezza == 136); /* minimo del tondo */
    VERIFICA(ph_dsk_ridimensiona(PH_CFG_WIDGET_BATTERIA, 0, (PhDskMisura){176, 176}, 9000, 0, 900, 900, 9000, &n, &destra));
    VERIFICA(n.larghezza == 320 && n.altezza == 320); /* massimo del tondo */
    /* Il tondo e' limitato dal lato piu' stretto dell'area: qui dal basso (altezza utile 300, alto 50: 250). */
    VERIFICA(ph_dsk_ridimensiona(PH_CFG_WIDGET_BATTERIA, 0, (PhDskMisura){176, 176}, 9000, 50, 400, 400, 300, &n, &destra));
    VERIFICA(n.larghezza == 250 && n.altezza == 250);
    /* La capsula tiene l'altezza entro 128. */
    VERIFICA(ph_dsk_ridimensiona(PH_CFG_WIDGET_SVEGLIA, 0, (PhDskMisura){250, 80}, 100, 20, 0, 500, 900, &n, &destra));
    VERIFICA(n.altezza == 128 && n.larghezza == 250);
    /* Dopo ogni calcolo la misura e' dentro i limiti e sempre sul tipo giusto. */
    for (int dxx = -300; dxx <= 300; dxx += 37)
        for (int dyy = -300; dyy <= 300; dyy += 41) {
            for (size_t i = 0; i < sizeof tutti / sizeof tutti[0]; i++) {
                PhCfgWidgetTipo t = tutti[i];
                PhDskLimiti lim = ph_dsk_misura_limiti(t, 0);
                PhDskMisura b = ph_dsk_misura_equivalente(t, 0, PH_CFG_WIDGET_MEDIO);
                ph_dsk_ridimensiona(t, 0, b, 40, 40, dxx, dyy, 800, &n, &destra);
                VERIFICA(n.larghezza >= lim.min.larghezza && n.larghezza <= lim.max.larghezza);
                VERIFICA(n.altezza >= lim.min.altezza && n.altezza <= lim.max.altezza);
                VERIFICA(destra >= 0 && 40 + n.altezza <= 800);
                if (ph_dsk_forma(t, 0) == PH_DSK_FORMA_TONDO)
                    VERIFICA(n.larghezza == n.altezza);
            }
        }

    /* Cambio di stile: forma uguale = la misura si tiene (entro i limiti del nuovo stile); forma
     * diversa = si perde ({0, 0}). */
    m = ph_dsk_misura_cambio_stile(PH_CFG_WIDGET_OROLOGIO, PH_DSK_OROLOGIO_DIGITALE, PH_DSK_OROLOGIO_TESTUALE,
                                   (PhDskMisura){300, 200});
    VERIFICA(m.larghezza == 300 && m.altezza == 200);
    m = ph_dsk_misura_cambio_stile(PH_CFG_WIDGET_OROLOGIO, PH_DSK_OROLOGIO_DIGITALE, PH_DSK_OROLOGIO_TESTUALE,
                                   (PhDskMisura){250, 90}); /* sotto il minimo del testuale (106) */
    VERIFICA(m.larghezza == 250 && m.altezza == 106);
    m = ph_dsk_misura_cambio_stile(PH_CFG_WIDGET_OROLOGIO, PH_DSK_OROLOGIO_CLASSICO, PH_DSK_OROLOGIO_ESSENZIALE,
                                   (PhDskMisura){144, 144}); /* tondo -> tondo, minimo 152 */
    VERIFICA(m.larghezza == 152 && m.altezza == 152);
    m = ph_dsk_misura_cambio_stile(PH_CFG_WIDGET_OROLOGIO, PH_DSK_OROLOGIO_CLASSICO, PH_DSK_OROLOGIO_ESSENZIALE,
                                   (PhDskMisura){200, 200});
    VERIFICA(m.larghezza == 200 && m.altezza == 200);
    m = ph_dsk_misura_cambio_stile(PH_CFG_WIDGET_OROLOGIO, PH_DSK_OROLOGIO_DIGITALE, PH_DSK_OROLOGIO_CLASSICO,
                                   (PhDskMisura){300, 200});
    VERIFICA(m.larghezza == 0 && m.altezza == 0);
    m = ph_dsk_misura_cambio_stile(PH_CFG_WIDGET_OROLOGIO, PH_DSK_OROLOGIO_ESSENZIALE, PH_DSK_OROLOGIO_PALETTE,
                                   (PhDskMisura){200, 200});
    VERIFICA(m.larghezza == 0 && m.altezza == 0);
    m = ph_dsk_misura_cambio_stile(PH_CFG_WIDGET_METEO, 0, 3, (PhDskMisura){400, 500}); /* stessa forma */
    VERIFICA(m.larghezza == 400 && m.altezza == 500);

    /* Maniglia: dentro la curva; 0 dove l'angolo e' poco curvo. */
    VERIFICA(ph_dsk_maniglia_margine(28, 20) == 0 && ph_dsk_maniglia_margine(28, 16) == 0);
    VERIFICA(ph_dsk_maniglia_margine(92, 12) == 14); /* 92*0,2929 + 7,07 - 8 - 12 = 14,0 */
    VERIFICA(ph_dsk_maniglia_margine(36, 24) == 0 && ph_dsk_maniglia_margine(36, 10) == 0);
    VERIFICA(ph_dsk_maniglia_margine(160, 12) > ph_dsk_maniglia_margine(92, 12));
    VERIFICA(ph_dsk_maniglia_margine(0, 0) == 0 && ph_dsk_maniglia_margine(-10, 0) == 0);
}

static void prova_tavolozza_e_cartelle(void)
{
    /* 48 colori tutti diversi, la prima riga di grigi, oltre il fondo zero. */
    bool doppi = false;
    for (size_t i = 0; i < PH_DSK_PALETTE_N; i++)
        for (size_t j = i + 1; j < PH_DSK_PALETTE_N; j++)
            if (ph_dsk_palette(i) == ph_dsk_palette(j))
                doppi = true;
    VERIFICA(!doppi);
    VERIFICA(ph_dsk_palette(0) == 0x000000 && ph_dsk_palette(7) == 0xffffff);
    VERIFICA(ph_dsk_palette(PH_DSK_PALETTE_N) == 0);
    for (size_t i = 0; i < PH_DSK_PALETTE_N; i++)
        VERIFICA(ph_dsk_palette(i) <= 0xffffff);

    char o[128];
    VERIFICA(ph_dsk_tel_nome_valido("DCIM") && ph_dsk_tel_nome_valido("Foto è bella"));
    VERIFICA(!ph_dsk_tel_nome_valido("") && !ph_dsk_tel_nome_valido(".") &&
             !ph_dsk_tel_nome_valido("..") && !ph_dsk_tel_nome_valido("a/b") &&
             !ph_dsk_tel_nome_valido("a\nb") && !ph_dsk_tel_nome_valido("a\x7f") &&
             !ph_dsk_tel_nome_valido("\xff\xfe") && !ph_dsk_tel_nome_valido(NULL));
    char lungo[300];
    memset(lungo, 'x', sizeof lungo - 1);
    lungo[sizeof lungo - 1] = '\0';
    VERIFICA(!ph_dsk_tel_nome_valido(lungo));
    VERIFICA(ph_dsk_tel_nascosto(".nomedia") && !ph_dsk_tel_nascosto("a.jpg"));
    VERIFICA(ph_dsk_tel_unisci("/storage/emulated/0", "DCIM", o, sizeof o));
    VERIFICA_STR(o, "/storage/emulated/0/DCIM");
    VERIFICA(ph_dsk_tel_unisci("/", "data", o, sizeof o));
    VERIFICA_STR(o, "/data");
    VERIFICA(!ph_dsk_tel_unisci("/a", "..", o, sizeof o) && o[0] == '\0');
    VERIFICA(!ph_dsk_tel_unisci("relativo", "a", o, sizeof o));
    VERIFICA(!ph_dsk_tel_unisci("/a", "b/c", o, sizeof o));
    VERIFICA(!ph_dsk_tel_unisci("/a", "b", o, 3)); /* non sta */
    VERIFICA(ph_dsk_tel_su("/storage/emulated/0", o, sizeof o));
    VERIFICA_STR(o, "/storage/emulated");
    VERIFICA(ph_dsk_tel_su("/data", o, sizeof o));
    VERIFICA_STR(o, "/");
    VERIFICA(ph_dsk_tel_su("/a/b/", o, sizeof o));
    VERIFICA_STR(o, "/a");
    VERIFICA(!ph_dsk_tel_su("/", o, sizeof o));
    VERIFICA_STR(o, "/");
    VERIFICA(!ph_dsk_tel_su("senza barra", o, sizeof o));
    VERIFICA(!ph_dsk_tel_su(NULL, o, sizeof o));
    VERIFICA(ph_dsk_tel_e_radice("/") && !ph_dsk_tel_e_radice("/a"));
    /* Salire fino a "/" da qualunque profondita'. */
    char p[PH_DSK_TEL_PERCORSO_MAX];
    snprintf(p, sizeof p, "/a/b/c/d");
    int passi = 0;
    while (ph_dsk_tel_su(p, o, sizeof o) && passi < 20) {
        snprintf(p, sizeof p, "%s", o);
        passi++;
    }
    VERIFICA(passi == 4 && strcmp(p, "/") == 0);

    VERIFICA(ph_dsk_pagina_visibili(500, 1, 200) == 200);
    VERIFICA(ph_dsk_pagina_visibili(500, 3, 200) == 500);
    VERIFICA(ph_dsk_pagina_visibili(50, 1, 200) == 50);
    VERIFICA(ph_dsk_pagina_visibili(0, 1, 200) == 0);
    VERIFICA(ph_dsk_pagina_visibili(10, (size_t)-1, 200) == 10); /* nessun overflow */
    VERIFICA(ph_dsk_pagina_prossime(500, 200, 200) == 200);
    VERIFICA(ph_dsk_pagina_prossime(500, 400, 200) == 100);
    VERIFICA(ph_dsk_pagina_prossime(500, 500, 200) == 0);
}

static void prova_orologio_stili(void)
{
    ph_lingua_imposta(PH_LINGUA_IT);
    char o[160];
    static const struct {
        int h, m;
        const char *atteso;
    } casi[] = {{0, 0, "Mezzanotte"},
                {12, 0, "Mezzogiorno"},
                {16, 0, "Le quattro in punto"},
                {1, 0, "L'una in punto"},
                {13, 0, "L'una in punto"},
                {12, 15, "Mezzogiorno e un quarto"},
                {16, 50, "Le cinque meno dieci"},
                {3, 45, "Le quattro meno un quarto"},
                {1, 30, "L'una e mezza"},
                {0, 30, "Mezzanotte e mezza"},
                {11, 59, "Mezzogiorno meno un minuto"},
                {23, 59, "Mezzanotte meno un minuto"},
                {0, 1, "Mezzanotte e un minuto"},
                {4, 5, "Le quattro e cinque"},
                {4, 29, "Le quattro e ventinove"},
                {4, 31, "Le quattro e trentuno"},
                {4, 34, "Le quattro e trentaquattro"},
                {4, 35, "Le cinque meno venticinque"},
                {4, 59, "Le cinque meno un minuto"},
                {3, 33, "Le tre e trentatré"},
                {4, 23, "Le quattro e ventitré"},
                {11, 50, "Mezzogiorno meno dieci"},
                {22, 40, "Le undici meno venti"},
                {11, 15, "Le undici e un quarto"},
                {23, 45, "Mezzanotte meno un quarto"}};
    for (size_t i = 0; i < sizeof casi / sizeof casi[0]; i++) {
        ph_dsk_ora_testo(casi[i].h, casi[i].m, o, sizeof o);
        if (strcmp(o, casi[i].atteso) != 0) {
            fprintf(stderr, "ora %02d:%02d: \"%s\" != \"%s\"\n", casi[i].h, casi[i].m, o, casi[i].atteso);
            errori++;
        }
    }
    ph_dsk_ora_testo(24, 0, o, sizeof o);
    VERIFICA_STR(o, "");
    ph_dsk_ora_testo(1, 60, o, sizeof o);
    VERIFICA_STR(o, "");
    ph_dsk_ora_testo(-1, 0, o, sizeof o);
    VERIFICA_STR(o, "");
    /* Tutti i 1440 minuti: una frase non vuota, senza segnaposto rimasti. */
    bool tutte = true;
    for (int h = 0; h < 24; h++)
        for (int m = 0; m < 60; m++) {
            ph_dsk_ora_testo(h, m, o, sizeof o);
            if (o[0] == '\0' || strchr(o, '{') != NULL || strchr(o, '}') != NULL)
                tutte = false;
        }
    VERIFICA(tutte);
    char piccolo[6]; /* troncata senza uscire dal buffer */
    ph_dsk_ora_testo(4, 29, piccolo, sizeof piccolo);
    VERIFICA(strlen(piccolo) < sizeof piccolo);
    ph_lingua_imposta(PH_LINGUA_EN);
    ph_dsk_ora_testo(16, 50, o, sizeof o);
    VERIFICA_STR(o, "Ten to five");
    ph_dsk_ora_testo(12, 15, o, sizeof o);
    VERIFICA_STR(o, "Quarter past noon");
    ph_dsk_ora_testo(16, 0, o, sizeof o);
    VERIFICA_STR(o, "Four o'clock");
    ph_dsk_ora_testo(1, 30, o, sizeof o);
    VERIFICA_STR(o, "Half past one");
    ph_dsk_ora_testo(0, 0, o, sizeof o);
    VERIFICA_STR(o, "Midnight");
    ph_dsk_ora_testo(3, 31, o, sizeof o);
    VERIFICA_STR(o, "Three thirty-one");
    ph_dsk_ora_testo(4, 35, o, sizeof o);
    VERIFICA_STR(o, "Twenty-five to five");
    ph_dsk_ora_testo(4, 29, o, sizeof o);
    VERIFICA_STR(o, "Twenty-nine past four");
    VERIFICA_STR(ph_dsk_stile_nome(PH_DSK_OROLOGIO_DOPPIO_FUSO), "Dual time zone");
    ph_lingua_imposta(PH_LINGUA_IT);

    /* Angoli delle lancette (gradi in senso orario da ore 12). */
    double ah, am, as;
    ph_dsk_angoli_lancette(3, 0, 0, &ah, &am, &as);
    VERIFICA(vicino((float)ah, 90) && vicino((float)am, 0) && vicino((float)as, 0));
    ph_dsk_angoli_lancette(12, 0, 0, &ah, &am, &as);
    VERIFICA(vicino((float)ah, 0));
    ph_dsk_angoli_lancette(6, 30, 0, &ah, &am, &as);
    VERIFICA(vicino((float)ah, 195) && vicino((float)am, 180));
    ph_dsk_angoli_lancette(9, 15, 30, &ah, &am, &as);
    VERIFICA(vicino((float)as, 180) && vicino((float)am, 93) && vicino((float)ah, 277.75f));
    ph_dsk_angoli_lancette(23, 59, 59.5, &ah, &am, &as);
    VERIFICA(ah < 360 && am < 360 && as < 360);
    ph_dsk_angoli_lancette(99, -3, -5, &ah, &am, &as); /* valori assurdi: nessun valore fuori scala */
    VERIFICA(ah >= 0 && ah < 360 && am >= 0 && am < 360 && as >= 0 && as < 360);

    /* Attesa fino al prossimo aggiornamento. */
    VERIFICA(ph_dsk_orologio_attesa_ms(0, true) == 1005);
    VERIFICA(ph_dsk_orologio_attesa_ms(250, true) == 755);
    VERIFICA(ph_dsk_orologio_attesa_ms(0, false) == 60005);
    VERIFICA(ph_dsk_orologio_attesa_ms(59990, false) == 20); /* 15 ms: alzato al minimo */
    VERIFICA(ph_dsk_orologio_attesa_ms(59999, false) >= 20); /* mai meno di 20 ms */

    /* Stili. */
    VERIFICA(ph_dsk_stile_valido(3) == 3 && ph_dsk_stile_valido(99) == 0);
    for (unsigned i = 0; i < PH_DSK_OROLOGIO_STILI; i++)
        VERIFICA(ph_dsk_stile_nome(i) != NULL && ph_dsk_stile_nome(i)[0] != '\0');
    VERIFICA(ph_dsk_stile_nome(PH_DSK_OROLOGIO_STILI) == NULL);
    VERIFICA(ph_dsk_stile_disegnato(PH_DSK_OROLOGIO_CLASSICO) && !ph_dsk_stile_disegnato(PH_DSK_OROLOGIO_TESTUALE) &&
             !ph_dsk_stile_disegnato(PH_DSK_OROLOGIO_DIGITALE));
    VERIFICA(ph_dsk_stile_con_secondi(PH_DSK_OROLOGIO_CLASSICO) && !ph_dsk_stile_con_secondi(PH_DSK_OROLOGIO_ESSENZIALE));
    VERIFICA(ph_dsk_orologio_altezza(PH_DSK_OROLOGIO_DIGITALE, PH_CFG_WIDGET_MEDIO) == 0);
    VERIFICA(ph_dsk_orologio_altezza(PH_DSK_OROLOGIO_CLASSICO, PH_CFG_WIDGET_MEDIO) == 160);
    VERIFICA(ph_dsk_orologio_altezza(PH_DSK_OROLOGIO_ESSENZIALE, PH_CFG_WIDGET_PICCOLO) == 124);
    /* Il quadrante sta nella scheda: diametro <= larghezza interna (larghezza - 34). */
    for (int dd = 0; dd < 3; dd++)
        for (unsigned st = 0; st < PH_DSK_OROLOGIO_STILI; st++)
            VERIFICA(ph_dsk_quadrante_diametro(st, (PhCfgWidgetDim)dd) <=
                     ph_dsk_larghezza_widget((PhCfgWidgetDim)dd) - 34);

    /* Fusi: 2026-11-23 10:00 UTC, locale = UTC+1. Senza database dei fusi si salta. */
    int ore, minuti, gd;
    VERIFICA(ph_dsk_citta_n() == 8 && ph_dsk_citta_nome(0) != NULL && ph_dsk_citta_nome(8) == NULL);
    VERIFICA(ph_dsk_citta_valida(5) == 5 && ph_dsk_citta_valida(77) == 0);
    VERIFICA(!ph_dsk_ora_in_citta(1795428000000ull, 3600, 99, &ore, &minuti, &gd));
    if (ph_dsk_ora_in_citta(1795428000000ull, 3600, 0, &ore, &minuti, &gd)) {
        VERIFICA(ore == 10 && minuti == 0 && gd == 0);                 /* Londra (inverno) */
        VERIFICA(ph_dsk_ora_in_citta(1795428000000ull, 3600, 6, &ore, &minuti, &gd) && ore == 19 && gd == 0);
        VERIFICA(ph_dsk_ora_in_citta(1795428000000ull, 3600, 1, &ore, &minuti, &gd) && ore == 5 && gd == 0);
        VERIFICA(ph_dsk_ora_in_citta(1795428000000ull, 3600, 7, &ore, &minuti, &gd) && ore == 21 && gd == 0);
        /* 20:30 UTC: a Tokyo e Sydney e' gia' il giorno dopo, a New York no. */
        VERIFICA(ph_dsk_ora_in_citta(1795465800000ull, 3600, 6, &ore, &minuti, &gd) && ore == 5 && minuti == 30 && gd == 1);
        VERIFICA(ph_dsk_ora_in_citta(1795465800000ull, 3600, 7, &ore, &minuti, &gd) && ore == 7 && gd == 1);
        VERIFICA(ph_dsk_ora_in_citta(1795465800000ull, 3600, 1, &ore, &minuti, &gd) && ore == 15 && gd == 0);
        /* Locale a UTC+14 (a mezzanotte del giorno dopo) e New York indietro: ieri. */
        VERIFICA(ph_dsk_ora_in_citta(1795428000000ull, 14 * 3600, 1, &ore, &minuti, &gd) && gd == -1);
    } else {
        fprintf(stderr, "prova_desktop: database dei fusi assente, prova dei fusi saltata\n");
    }
    ph_dsk_ora_in_citta(0, 0, 0, &ore, &minuti, &gd); /* istante zero: nessun crash */

    /* Viste. */
    PhDskVista v;
    ph_dsk_vista_orologio_testo(PH_CFG_WIDGET_MEDIO, "Le quattro meno dieci", "Lunedì 23 novembre", &v);
    VERIFICA_STR(v.testa, "Le quattro meno dieci");
    VERIFICA_STR(v.sotto, "Lunedì 23 novembre");
    VERIFICA(!v.testa_grande);
    ph_dsk_vista_orologio_testo(PH_CFG_WIDGET_PICCOLO, "x", "data", &v);
    VERIFICA_STR(v.sotto, "");
    ph_dsk_vista_orologio_fuso(PH_CFG_WIDGET_MEDIO, "10:08", "Lunedì 23 novembre", "Tokyo", true, 19, 8, 0, &v);
    VERIFICA_STR(v.testa, "10:08");
    VERIFICA(v.testa_grande && v.n_righe == 1);
    VERIFICA_STR(v.righe[0].sinistra, "Tokyo");
    VERIFICA_STR(v.righe[0].destra, "19:08");
    ph_dsk_vista_orologio_fuso(PH_CFG_WIDGET_MEDIO, "10:08", "d", "Tokyo", true, 5, 30, 1, &v);
    VERIFICA_STR(v.righe[0].destra, "05:30 +1");
    ph_dsk_vista_orologio_fuso(PH_CFG_WIDGET_PICCOLO, "10:08", "d", "Tokyo", false, 0, 0, 0, &v);
    VERIFICA_STR(v.righe[0].destra, "--:--"); /* fuso non disponibile: nessun numero inventato */
    VERIFICA_STR(v.sotto, "");
}

/* ---- Widget nuovi: calendario del mese, sveglia, batteria --------------------------------------- */
static uint64_t ms_utc(int a, int m, int g, int h, int mi)
{
    GDateTime *d = g_date_time_new_utc(a, m, g, h, mi, 0);
    uint64_t ms = (uint64_t)g_date_time_to_unix(d) * 1000u;
    g_date_time_unref(d);
    return ms;
}

static void prova_calendario_mese(void)
{
    ph_lingua_imposta(PH_LINGUA_IT);
    VERIFICA(ph_dsk_giorno_civile(1970, 1, 1) == 0);
    VERIFICA(ph_dsk_giorno_civile(2026, 11, 23) == 20780);
    int a, m, g;
    ph_dsk_civile_da_giorno(20780, &a, &m, &g);
    VERIFICA(a == 2026 && m == 11 && g == 23);
    /* Andata e ritorno per ogni giorno di 130 anni. */
    bool giro = true;
    for (int64_t d = 0; d < 365 * 131; d++) {
        ph_dsk_civile_da_giorno(d, &a, &m, &g);
        if (ph_dsk_giorno_civile(a, m, g) != d || m < 1 || m > 12 || g < 1 || g > ph_dsk_giorni_nel_mese(a, m))
            giro = false;
    }
    VERIFICA(giro);
    VERIFICA(ph_dsk_giorno_settimana(2026, 11, 23) == 0); /* lunedi' */
    VERIFICA(ph_dsk_giorno_settimana(1970, 1, 1) == 3);   /* giovedi' */
    VERIFICA(ph_dsk_giorno_settimana(2024, 2, 29) == 3);
    VERIFICA(ph_dsk_inizio_settimana(ph_dsk_giorno_civile(2026, 11, 29)) == ph_dsk_giorno_civile(2026, 11, 23));
    VERIFICA(ph_dsk_giorni_nel_mese(2024, 2) == 29 && ph_dsk_giorni_nel_mese(2026, 2) == 28 &&
             ph_dsk_giorni_nel_mese(2000, 2) == 29 && ph_dsk_giorni_nel_mese(2100, 2) == 28 &&
             ph_dsk_giorni_nel_mese(1900, 2) == 28);
    VERIFICA(ph_dsk_giorni_nel_mese(2026, 0) == 0 && ph_dsk_giorni_nel_mese(2026, 13) == 0);
    static const int g2026[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    for (int mm = 1; mm <= 12; mm++)
        VERIFICA(ph_dsk_giorni_nel_mese(2026, mm) == g2026[mm - 1]);

    /* La griglia di ogni mese dal 1970 al 2100: ogni giorno compare una volta, nella colonna giusta. */
    bool griglia_ok = true;
    for (int anno = 1970; anno <= 2100; anno++)
        for (int mese = 1; mese <= 12; mese++) {
            PhDskMese mm;
            if (!ph_dsk_mese(anno, mese, &mm) || mm.righe < 4 || mm.righe > 6) {
                griglia_ok = false;
                continue;
            }
            int visti = 0;
            for (int c = 0; c < mm.righe * 7; c++) {
                int gg = ph_dsk_mese_cella(&mm, c);
                if (gg == 0)
                    continue;
                visti++;
                if (gg != visti || ph_dsk_giorno_settimana(anno, mese, gg) != c % 7)
                    griglia_ok = false;
            }
            if (visti != mm.giorni || mm.primo != ph_dsk_giorno_settimana(anno, mese, 1))
                griglia_ok = false;
        }
    VERIFICA(griglia_ok);
    PhDskMese mm;
    VERIFICA(ph_dsk_mese(2026, 11, &mm) && mm.primo == 6 && mm.giorni == 30 && mm.righe == 6); /* novembre 2026 */
    VERIFICA(ph_dsk_mese(2027, 2, &mm) && mm.primo == 0 && mm.giorni == 28 && mm.righe == 4);  /* quattro righe */
    VERIFICA(!ph_dsk_mese(2026, 0, &mm) && !ph_dsk_mese(1800, 5, &mm) && !ph_dsk_mese(2026, 13, &mm));
    VERIFICA(ph_dsk_mese_cella(&mm, -1) == 0 && ph_dsk_mese_cella(&mm, 1000) == 0);
    int an = 2026, me = 12;
    ph_dsk_mese_sposta(&an, &me, 1);
    VERIFICA(an == 2027 && me == 1);
    ph_dsk_mese_sposta(&an, &me, -2);
    VERIFICA(an == 2026 && me == 11);
    ph_dsk_mese_sposta(&an, &me, -100000);
    VERIFICA(an == 1970 && me == 1);
    ph_dsk_mese_sposta(&an, &me, 100000);
    VERIFICA(an == 2100 && me == 12);
    char t[64];
    ph_dsk_mese_titolo(2026, 11, t, sizeof t);
    VERIFICA_STR(t, "Novembre 2026");
    ph_lingua_imposta(PH_LINGUA_EN);
    ph_dsk_mese_titolo(2026, 11, t, sizeof t);
    VERIFICA_STR(t, "November 2026");
    ph_lingua_imposta(PH_LINGUA_IT);

    GTimeZone *roma = g_time_zone_new_identifier("Europe/Rome");
    if (roma == NULL) {
        fprintf(stderr, "prova_desktop: fuso di Roma assente, prova dei giorni saltata\n");
        return;
    }
    uint64_t da, a_ms;
    /* Marzo 2026 ha il cambio all'ora legale (29 marzo): 743 ore. Ottobre: 745. */
    VERIFICA(ph_dsk_mese_intervallo(roma, 2026, 3, &da, &a_ms) && da == ms_utc(2026, 2, 28, 23, 0) &&
             a_ms - da == 743ull * 3600000ull);
    VERIFICA(ph_dsk_mese_intervallo(roma, 2026, 10, &da, &a_ms) && a_ms - da == 745ull * 3600000ull);
    VERIFICA(ph_dsk_mese_intervallo(roma, 2026, 11, &da, &a_ms) && da == ms_utc(2026, 10, 31, 23, 0) &&
             a_ms == ms_utc(2026, 11, 30, 23, 0));
    VERIFICA(!ph_dsk_mese_intervallo(roma, 2026, 13, &da, &a_ms));
    VERIFICA(!ph_dsk_giorni_intervallo(roma, 20780, 63, &da, &a_ms)); /* mai oltre 62 giorni */
    VERIFICA(ph_dsk_giorni_intervallo(roma, 20780, 62, &da, &a_ms));
    VERIFICA(!ph_dsk_giorni_intervallo(roma, 20780, 0, &da, &a_ms));

    /* Giorno locale: a mezzanotte e mezza dell'ora legale e' gia' il giorno dopo. */
    VERIFICA(ph_dsk_giorno_locale(roma, ms_utc(2026, 7, 1, 22, 30)) == ph_dsk_giorno_civile(2026, 7, 2));
    VERIFICA(ph_dsk_giorno_locale(roma, ms_utc(2026, 12, 1, 22, 30)) == ph_dsk_giorno_civile(2026, 12, 1));
    VERIFICA(ph_dsk_giorno_locale(roma, ms_utc(2026, 12, 1, 23, 30)) == ph_dsk_giorno_civile(2026, 12, 2));
    VERIFICA(ph_dsk_giorno_locale(roma, ms_utc(2026, 3, 28, 23, 30)) == ph_dsk_giorno_civile(2026, 3, 29));
    VERIFICA(ph_dsk_giorno_locale(roma, ms_utc(2026, 3, 29, 23, 30)) == ph_dsk_giorno_civile(2026, 3, 30));

    /* Eventi sui giorni (CONTRATTI.md §16, aggiunta). */
    int64_t d22 = ph_dsk_giorno_civile(2026, 11, 22), d23 = d22 + 1, d24 = d22 + 2, d25 = d22 + 3;
    int64_t pr, ul;
    PhDskEvento domenica = {"d", ms_utc(2026, 11, 22, 0, 0), ms_utc(2026, 11, 23, 0, 0), true};
    VERIFICA(ph_dsk_evento_giorni(&domenica, roma, &pr, &ul) && pr == d22 && ul == d22); /* solo la domenica */
    PhDskEvento tre = {"t", ms_utc(2026, 11, 22, 0, 0), ms_utc(2026, 11, 25, 0, 0), true};
    VERIFICA(ph_dsk_evento_giorni(&tre, roma, &pr, &ul) && pr == d22 && ul == d24);
    PhDskEvento senza_durata = {"z", ms_utc(2026, 11, 22, 0, 0), ms_utc(2026, 11, 22, 0, 0), true};
    VERIFICA(ph_dsk_evento_giorni(&senza_durata, roma, &pr, &ul) && pr == d22 && ul == d22);
    /* «Tutto il giorno» si legge in UTC anche con un fuso lontano (Auckland, UTC+13): sempre la domenica. */
    GTimeZone *auck = g_time_zone_new_identifier("Pacific/Auckland");
    if (auck != NULL) {
        VERIFICA(ph_dsk_evento_giorni(&domenica, auck, &pr, &ul) && pr == d22 && ul == d22);
        g_time_zone_unref(auck);
    }
    /* Finisce esattamente a mezzanotte locale (23:00Z): non conta il giorno dopo. */
    PhDskEvento fino_mezzanotte = {"m", ms_utc(2026, 11, 22, 22, 0), ms_utc(2026, 11, 22, 23, 0), false};
    VERIFICA(ph_dsk_evento_giorni(&fino_mezzanotte, roma, &pr, &ul) && pr == d22 && ul == d22);
    PhDskEvento a_cavallo = {"c", ms_utc(2026, 11, 22, 22, 30), ms_utc(2026, 11, 22, 23, 30), false}; /* 23:30-00:30 */
    VERIFICA(ph_dsk_evento_giorni(&a_cavallo, roma, &pr, &ul) && pr == d22 && ul == d23);
    PhDskEvento estate = {"e", ms_utc(2026, 7, 1, 22, 30), ms_utc(2026, 7, 1, 23, 0), false}; /* 00:30 CEST del 2 */
    VERIFICA(ph_dsk_evento_giorni(&estate, roma, &pr, &ul) && pr == ph_dsk_giorno_civile(2026, 7, 2));
    PhDskEvento rovescio = {"r", 2000, 1000, false};
    VERIFICA(!ph_dsk_evento_giorni(&rovescio, roma, &pr, &ul));
    PhDskEvento assurdo = {"a", UINT64_MAX - 5, UINT64_MAX, false};
    VERIFICA(!ph_dsk_evento_giorni(&assurdo, roma, &pr, &ul));

    PhDskEvento ev[] = {domenica, tre, fino_mezzanotte, a_cavallo, rovescio,
                        {"fuori", ms_utc(2026, 10, 2, 0, 0), ms_utc(2026, 10, 3, 0, 0), true}};
    uint32_t mask;
    size_t toccano = ph_dsk_mese_con_impegni(ev, 6, roma, 2026, 11, &mask);
    VERIFICA(toccano == 4); /* l'invalido e quello di ottobre non toccano novembre */
    VERIFICA(mask == ((1u << 21) | (1u << 22) | (1u << 23))); /* 22, 23 e 24 novembre */
    size_t idx[8];
    VERIFICA(ph_dsk_eventi_del_giorno(ev, 6, roma, d23, idx, 8) == 2 && idx[0] == 1 && idx[1] == 3);
    VERIFICA(ph_dsk_eventi_del_giorno(ev, 6, roma, d25, idx, 8) == 0);
    VERIFICA(ph_dsk_eventi_del_giorno(ev, 6, roma, d22, idx, 1) == 1);
    uint8_t conti[5];
    ph_dsk_conta_per_giorno(ev, 6, roma, d22, 5, conti);
    VERIFICA(conti[0] == 4 && conti[1] == 2 && conti[2] == 1 && conti[3] == 0 && conti[4] == 0);
    VERIFICA(ph_dsk_mese_con_impegni(NULL, 0, roma, 2026, 11, &mask) == 0 && mask == 0);
    /* Un mese di dicembre con un evento di fine novembre che arriva in dicembre. */
    PhDskEvento lungo = {"l", ms_utc(2026, 11, 29, 0, 0), ms_utc(2026, 12, 2, 0, 0), true};
    VERIFICA(ph_dsk_mese_con_impegni(&lungo, 1, roma, 2026, 12, &mask) == 1 && mask == 1u);
    VERIFICA(ph_dsk_mese_con_impegni(&lungo, 1, roma, 2027, 1, &mask) == 0 && mask == 0);

    char o[64];
    ph_dsk_evento_orario(&domenica, roma, o, sizeof o);
    VERIFICA_STR(o, "Tutto il giorno");
    PhDskEvento riunione = {"r", ms_utc(2026, 11, 23, 9, 30), ms_utc(2026, 11, 23, 10, 45), false};
    ph_dsk_evento_orario(&riunione, roma, o, sizeof o);
    VERIFICA_STR(o, "10:30\xE2\x80\x93" "11:45");
    PhDskEvento puntuale = {"p", ms_utc(2026, 11, 23, 9, 30), ms_utc(2026, 11, 23, 9, 30), false};
    ph_dsk_evento_orario(&puntuale, roma, o, sizeof o);
    VERIFICA_STR(o, "10:30");
    ph_dsk_evento_orario(&riunione, NULL, o, 4); /* buffer piccolo: nessuna uscita dal buffer */
    VERIFICA(strlen(o) < 4);

    ph_dsk_giorno_etichetta(d23, d23, o, sizeof o);
    VERIFICA_STR(o, "Oggi");
    ph_dsk_giorno_etichetta(d24, d23, o, sizeof o);
    VERIFICA_STR(o, "Domani");
    ph_dsk_giorno_etichetta(d25, d23, o, sizeof o);
    VERIFICA_STR(o, "Mer 25");
    ph_lingua_imposta(PH_LINGUA_EN);
    ph_dsk_giorno_etichetta(d25, d23, o, sizeof o);
    VERIFICA_STR(o, "Wed 25");
    ph_lingua_imposta(PH_LINGUA_IT);

    /* Agenda: intestazione per giorno e i suoi impegni, nell'ordine. */
    PhDskEvento ag[] = {{"uno", ms_utc(2026, 11, 23, 9, 0), ms_utc(2026, 11, 23, 10, 0), false},
                        {"due", ms_utc(2026, 11, 25, 9, 0), ms_utc(2026, 11, 25, 10, 0), false},
                        {"tre", ms_utc(2026, 11, 25, 11, 0), ms_utc(2026, 11, 25, 12, 0), false}};
    PhDskAgendaRiga rr[16];
    size_t kr = ph_dsk_agenda(ag, 3, roma, d23, 14, rr, 16);
    VERIFICA(kr == 5 && rr[0].evento == (size_t)-1 && rr[0].giorno == d23 && rr[1].evento == 0 &&
             rr[2].evento == (size_t)-1 && rr[2].giorno == d25 && rr[3].evento == 1 && rr[4].evento == 2);
    VERIFICA(ph_dsk_agenda(ag, 3, roma, d23, 14, rr, 4) == 4); /* tetto delle righe */
    VERIFICA(ph_dsk_agenda(ag, 3, roma, d23, 14, rr, 1) == 0); /* niente intestazione senza posto per un impegno */
    VERIFICA(ph_dsk_agenda(ag, 3, roma, d23 + 5, 14, rr, 16) == 0);

    /* Vista del Piccolo. */
    PhDskVista v;
    ph_dsk_vista_calendario_mese_piccolo(PH_DSK_DATO_OK, "", ag, 3, roma, d23, &v);
    VERIFICA_STR(v.titolo, "Calendario");
    VERIFICA_STR(v.testa, "1 impegno");
    ph_dsk_vista_calendario_mese_piccolo(PH_DSK_DATO_OK, "", ag, 3, roma, d25, &v);
    VERIFICA_STR(v.testa, "2 impegni");
    ph_dsk_vista_calendario_mese_piccolo(PH_DSK_DATO_OK, "", ag, 3, roma, d24, &v);
    VERIFICA_STR(v.testa, "Nessun impegno");
    ph_dsk_vista_calendario_mese_piccolo(PH_DSK_DATO_NON_DISPONIBILE, "", ag, 3, roma, d23, &v);
    VERIFICA_STR(v.testa, "Non disponibile");
    ph_dsk_vista_calendario_mese_piccolo(PH_DSK_DATO_ERRORE, "Frase del telefono", ag, 3, roma, d23, &v);
    VERIFICA_STR(v.sotto, "Frase del telefono");
    ph_dsk_vista_calendario_mese_piccolo(PH_DSK_DATO_ATTESA, "", ag, 3, roma, d23, &v);
    VERIFICA(v.testa[0] == '\0' && v.sotto[0] == '\0');
    VERIFICA(ph_dsk_dato_da_esito(PH_SRV_ESITO_OK, NULL) == PH_DSK_DATO_OK &&
             ph_dsk_dato_da_esito(PH_SRV_ESITO_ERRORE, NULL) == PH_DSK_DATO_NON_DISPONIBILE &&
             ph_dsk_dato_da_esito(PH_SRV_ESITO_ERRORE, "") == PH_DSK_DATO_NON_DISPONIBILE &&
             ph_dsk_dato_da_esito(PH_SRV_ESITO_ERRORE, "x") == PH_DSK_DATO_ERRORE);

    /* Sveglia: 2026-11-23 10:00Z (11:00 a Roma). */
    uint64_t adesso = ms_utc(2026, 11, 23, 10, 0);
    ph_dsk_vista_sveglia(PH_CFG_WIDGET_GRANDE, PH_DSK_DATO_OK, "", true, ms_utc(2026, 11, 24, 6, 30), adesso, roma, &v);
    VERIFICA_STR(v.titolo, "Prossima sveglia");
    VERIFICA_STR(v.testa, "07:30");
    VERIFICA_STR(v.sotto, "Domani");
    VERIFICA(v.testa_grande && v.n_righe == 1);
    VERIFICA_STR(v.righe[0].sinistra, "fra 20 h 30 min");
    ph_dsk_vista_sveglia(PH_CFG_WIDGET_MEDIO, PH_DSK_DATO_OK, "", true, ms_utc(2026, 11, 23, 18, 0), adesso, roma, &v);
    VERIFICA_STR(v.testa, "19:00");
    VERIFICA_STR(v.sotto, "Oggi");
    VERIFICA(v.n_righe == 0);
    ph_dsk_vista_sveglia(PH_CFG_WIDGET_GRANDE, PH_DSK_DATO_OK, "", true, ms_utc(2026, 11, 23, 18, 0), adesso, roma, &v);
    VERIFICA_STR(v.righe[0].sinistra, "fra 8 ore");
    ph_dsk_vista_sveglia(PH_CFG_WIDGET_PICCOLO, PH_DSK_DATO_OK, "", true, ms_utc(2026, 11, 27, 6, 30), adesso, roma, &v);
    VERIFICA_STR(v.sotto, "Ven 27");
    ph_dsk_vista_sveglia(PH_CFG_WIDGET_GRANDE, PH_DSK_DATO_OK, "", false, 0, adesso, roma, &v);
    VERIFICA_STR(v.testa, "Nessuna sveglia");
    ph_dsk_vista_sveglia(PH_CFG_WIDGET_GRANDE, PH_DSK_DATO_NON_DISPONIBILE, "", false, 0, adesso, roma, &v);
    VERIFICA_STR(v.testa, "Non disponibile");
    ph_dsk_vista_sveglia(PH_CFG_WIDGET_GRANDE, PH_DSK_DATO_OK, "", true, adesso - 3600000, adesso, roma, &v); /* gia' suonata */
    VERIFICA_STR(v.righe[0].sinistra, "fra meno di un minuto");
    g_time_zone_unref(roma);

    ph_dsk_fra_testo(0, o, sizeof o);
    VERIFICA_STR(o, "fra meno di un minuto");
    ph_dsk_fra_testo(1, o, sizeof o);
    VERIFICA_STR(o, "fra un minuto");
    ph_dsk_fra_testo(25, o, sizeof o);
    VERIFICA_STR(o, "fra 25 minuti");
    ph_dsk_fra_testo(60, o, sizeof o);
    VERIFICA_STR(o, "fra un'ora");
    ph_dsk_fra_testo(420, o, sizeof o);
    VERIFICA_STR(o, "fra 7 ore");
    ph_dsk_fra_testo(150, o, sizeof o);
    VERIFICA_STR(o, "fra 2 h 30 min");
    ph_dsk_fra_testo(24 * 60, o, sizeof o);
    VERIFICA_STR(o, "fra un giorno");
    ph_dsk_fra_testo(3 * 24 * 60 + 100, o, sizeof o);
    VERIFICA_STR(o, "fra 3 giorni");
    ph_dsk_fra_testo(-5, o, sizeof o);
    VERIFICA_STR(o, "fra meno di un minuto");
    ph_lingua_imposta(PH_LINGUA_EN);
    ph_dsk_fra_testo(420, o, sizeof o);
    VERIFICA_STR(o, "in 7 hours");
    ph_dsk_fra_testo(60, o, sizeof o);
    VERIFICA_STR(o, "in an hour");
    ph_lingua_imposta(PH_LINGUA_IT);

    /* Batteria. */
    PhSrvBatteria b = {.livello = 78, .stato = PH_SRV_BAT_IN_CARICA, .alimentazione = PH_SRV_ALIM_RETE,
                       .temperatura_nota = true, .temperatura_decimi = 315, .salute = PH_SRV_SALUTE_BUONA,
                       .tensione_mv = 4123, .minuti_carica = 80, .minuti_autonomia = 600};
    ph_dsk_vista_batteria(PH_CFG_WIDGET_GRANDE, PH_DSK_DATO_OK, "", &b, &v);
    VERIFICA_STR(v.titolo, "Batteria");
    VERIFICA_STR(v.testa, "78%");
    VERIFICA(v.percento == 78 && v.testa_grande);
    VERIFICA_STR(v.sotto, "In carica \xC2\xB7 rete");
    VERIFICA(v.n_righe == 4);
    VERIFICA_STR(v.righe[0].sinistra, "Temperatura");
    VERIFICA_STR(v.righe[0].destra, "31,5 \xC2\xB0" "C");
    VERIFICA_STR(v.righe[1].destra, "Buona");
    VERIFICA_STR(v.righe[2].destra, "4,12 V");
    VERIFICA_STR(v.righe[3].sinistra, "Carica completa tra");
    VERIFICA_STR(v.righe[3].destra, "1 h 20 min");
    for (size_t i = 0; i < v.n_righe; i++) /* mai l'autonomia stimata */
        VERIFICA(strstr(v.righe[i].sinistra, "utonomia") == NULL && strstr(v.righe[i].destra, "600") == NULL);
    ph_dsk_vista_batteria(PH_CFG_WIDGET_MEDIO, PH_DSK_DATO_OK, "", &b, &v);
    VERIFICA(v.n_righe == 1 && v.percento == 78);
    ph_dsk_vista_batteria(PH_CFG_WIDGET_PICCOLO, PH_DSK_DATO_OK, "", &b, &v);
    VERIFICA(v.n_righe == 0 && v.percento == 78);
    b.stato = PH_SRV_BAT_IN_SCARICA;
    b.minuti_carica = -1;
    b.temperatura_nota = false;
    b.tensione_mv = 0;
    b.salute = PH_SRV_SALUTE_SCONOSCIUTA;
    ph_dsk_vista_batteria(PH_CFG_WIDGET_GRANDE, PH_DSK_DATO_OK, "", &b, &v);
    VERIFICA_STR(v.sotto, "In scarica");
    VERIFICA(v.n_righe == 0); /* i dati non noti non si inventano */
    b.stato = PH_SRV_BAT_PIENA;
    b.livello = 250; /* fuori scala: ristretto */
    ph_dsk_vista_batteria(PH_CFG_WIDGET_MEDIO, PH_DSK_DATO_OK, "", &b, &v);
    VERIFICA_STR(v.sotto, "Carica completa");
    VERIFICA(v.percento == 100);
    b.stato = PH_SRV_BAT_IN_CARICA;
    b.alimentazione = PH_SRV_ALIM_USB | PH_SRV_ALIM_SENZA_FILI;
    ph_dsk_vista_batteria(PH_CFG_WIDGET_MEDIO, PH_DSK_DATO_OK, "", &b, &v);
    VERIFICA_STR(v.sotto, "In carica \xC2\xB7 USB");
    b.alimentazione = 0;
    ph_dsk_vista_batteria(PH_CFG_WIDGET_MEDIO, PH_DSK_DATO_OK, "", &b, &v);
    VERIFICA_STR(v.sotto, "In carica");
    ph_dsk_vista_batteria(PH_CFG_WIDGET_MEDIO, PH_DSK_DATO_NON_DISPONIBILE, "", NULL, &v);
    VERIFICA_STR(v.testa, "Non disponibile");
    VERIFICA(v.percento == -1);
    ph_dsk_vista_batteria(PH_CFG_WIDGET_MEDIO, PH_DSK_DATO_ERRORE, "Frase", NULL, &v);
    VERIFICA_STR(v.sotto, "Frase");
    ph_dsk_testo_temperatura(-25, o, sizeof o);
    VERIFICA_STR(o, "-2,5 \xC2\xB0" "C");
    ph_dsk_testo_durata(45, o, sizeof o);
    VERIFICA_STR(o, "45 min");
    ph_dsk_testo_durata(120, o, sizeof o);
    VERIFICA_STR(o, "2 h");
    ph_dsk_altri_testo(0, o, sizeof o);
    VERIFICA_STR(o, "");
    ph_dsk_altri_testo(1, o, sizeof o);
    VERIFICA_STR(o, "e un altro");
    ph_dsk_altri_testo(2, o, sizeof o);
    VERIFICA_STR(o, "e altri 2");
    ph_dsk_altri_testo(37, o, sizeof o);
    VERIFICA_STR(o, "e altri 37");
    ph_lingua_imposta(PH_LINGUA_EN);
    ph_dsk_altri_testo(1, o, sizeof o);
    VERIFICA_STR(o, "and 1 more");
    ph_dsk_altri_testo(5, o, sizeof o);
    VERIFICA_STR(o, "and 5 more");
    ph_lingua_imposta(PH_LINGUA_IT);
    VERIFICA(ph_dsk_stili_n(PH_CFG_WIDGET_OROLOGIO) == 6 && ph_dsk_stili_n(PH_CFG_WIDGET_CALENDARIO_MESE) == 3 &&
             ph_dsk_stili_n(PH_CFG_WIDGET_BATTERIA) == 1);
    VERIFICA(ph_dsk_stile_valido_di(PH_CFG_WIDGET_CALENDARIO_MESE, 3) == 0 && ph_dsk_stile_valido_di(PH_CFG_WIDGET_CALENDARIO_MESE, 2) == 2);
    VERIFICA_STR(ph_dsk_stile_nome_di(PH_CFG_WIDGET_CALENDARIO_MESE, 1), "Settimana");
    VERIFICA(ph_dsk_stile_nome_di(PH_CFG_WIDGET_CALENDARIO_MESE, 3) == NULL);
}

static void prova_wx_altri(void)
{
    ph_lingua_imposta(PH_LINGUA_IT);
    PhDskInterruttore ii;
    /* Suoneria: ciclo suona -> vibra -> silenzioso -> suona. */
    VERIFICA(ph_dsk_prossimo_valore(0, PH_SRV_SUONERIA_SUONA) == PH_SRV_SUONERIA_VIBRA);
    VERIFICA(ph_dsk_prossimo_valore(0, PH_SRV_SUONERIA_VIBRA) == PH_SRV_SUONERIA_SILENZIOSO);
    VERIFICA(ph_dsk_prossimo_valore(0, PH_SRV_SUONERIA_SILENZIOSO) == PH_SRV_SUONERIA_SUONA);
    VERIFICA(ph_dsk_prossimo_valore(0, 77) == PH_SRV_SUONERIA_SUONA); /* valore assurdo: si riparte */
    VERIFICA(ph_dsk_prossimo_valore(1, 0) == 1 && ph_dsk_prossimo_valore(1, 1) == 0);
    VERIFICA(ph_dsk_prossimo_valore(2, 0) == 1 && ph_dsk_prossimo_valore(2, 1) == 0);
    ph_dsk_interruttore(0, PH_SRV_SUONERIA_VIBRA, true, false, &ii);
    VERIFICA(ii.disponibile && ii.attivo && !ii.in_corso);
    VERIFICA_STR(ii.etichetta, "Suoneria");
    VERIFICA_STR(ii.stato, "Vibra");
    ph_dsk_interruttore(0, PH_SRV_SUONERIA_SUONA, true, false, &ii);
    VERIFICA(ii.disponibile && !ii.attivo);
    VERIFICA_STR(ii.stato, "Suona");
    ph_dsk_interruttore(0, PH_SRV_SUONERIA_SILENZIOSO, true, false, &ii);
    VERIFICA(ii.attivo);
    VERIFICA_STR(ii.stato, "Silenzioso");
    ph_dsk_interruttore(1, 1, true, false, &ii);
    VERIFICA_STR(ii.etichetta, "Non disturbare");
    VERIFICA(ii.attivo);
    VERIFICA_STR(ii.stato, "Acceso");
    ph_dsk_interruttore(2, 0, true, false, &ii);
    VERIFICA_STR(ii.etichetta, "Torcia");
    VERIFICA(!ii.attivo);
    VERIFICA_STR(ii.stato, "Spento");
    ph_dsk_interruttore(2, 1, true, true, &ii); /* dopo un clic: «in corso», mai uno stato presunto */
    VERIFICA(ii.in_corso);
    VERIFICA_STR(ii.stato, "In corso");
    ph_dsk_interruttore(2, PH_SRV_NON_DISPONIBILE, true, false, &ii);
    VERIFICA(!ii.disponibile && !ii.attivo);
    VERIFICA_STR(ii.stato, "Non disponibile");
    ph_dsk_interruttore(2, 1, false, false, &ii); /* puo_* falso: inattivo anche con un valore */
    VERIFICA(!ii.disponibile && !ii.attivo);
    VERIFICA_STR(ii.stato, "Non disponibile");
    ph_lingua_imposta(PH_LINGUA_EN);
    ph_dsk_interruttore(0, PH_SRV_SUONERIA_SILENZIOSO, true, false, &ii);
    VERIFICA_STR(ii.etichetta, "Ringer");
    VERIFICA_STR(ii.stato, "Silent");
    ph_lingua_imposta(PH_LINGUA_IT);

    /* Trova: conto alla rovescia di 60 secondi. */
    char t[48];
    VERIFICA(ph_dsk_trova_rimasti(60, 0) == 60 && ph_dsk_trova_rimasti(60, 1) == 60);
    VERIFICA(ph_dsk_trova_rimasti(60, 1000) == 59 && ph_dsk_trova_rimasti(60, 17400) == 43);
    VERIFICA(ph_dsk_trova_rimasti(60, 59001) == 1 && ph_dsk_trova_rimasti(60, 60000) == 0);
    VERIFICA(ph_dsk_trova_rimasti(60, 1000000) == 0 && ph_dsk_trova_rimasti(60, -500) == 60);
    VERIFICA(ph_dsk_trova_rimasti(0, 0) == 0 && ph_dsk_trova_rimasti(-5, 0) == 0);
    ph_dsk_trova_testo(60, t, sizeof t);
    VERIFICA_STR(t, "Resta 1:00");
    ph_dsk_trova_testo(42, t, sizeof t);
    VERIFICA_STR(t, "Resta 0:42");
    ph_dsk_trova_testo(-3, t, sizeof t);
    VERIFICA_STR(t, "Resta 0:00");
    ph_lingua_imposta(PH_LINGUA_EN);
    ph_dsk_trova_testo(9, t, sizeof t);
    VERIFICA_STR(t, "0:09 left");
    ph_lingua_imposta(PH_LINGUA_IT);

    /* App frequenti: piu' aperture prima, a pari merito la piu' recente. */
    PhCfgApertura ap[6];
    memset(ap, 0, sizeof ap);
    snprintf(ap[0].app, sizeof ap[0].app, "a");
    ap[0].volte = 5;
    ap[0].ultima = 100;
    snprintf(ap[1].app, sizeof ap[1].app, "b");
    ap[1].volte = 9;
    ap[1].ultima = 50;
    snprintf(ap[2].app, sizeof ap[2].app, "c");
    ap[2].volte = 5;
    ap[2].ultima = 300; /* come a ma piu' recente */
    snprintf(ap[3].app, sizeof ap[3].app, "d");
    ap[3].volte = 5;
    ap[3].ultima = 300; /* come c: vince l'ordine del profilo */
    snprintf(ap[4].app, sizeof ap[4].app, "e");
    ap[4].volte = 1;
    ap[4].ultima = 999;
    ap[5].volte = 7; /* nome vuoto: voce rovinata, scartata */
    size_t id[6];
    size_t k = ph_dsk_app_frequenti(ap, 6, 6, id);
    VERIFICA(k == 5 && id[0] == 1 && id[1] == 2 && id[2] == 3 && id[3] == 0 && id[4] == 4);
    k = ph_dsk_app_frequenti(ap, 6, 2, id);
    VERIFICA(k == 2 && id[0] == 1 && id[1] == 2);
    VERIFICA(ph_dsk_app_frequenti(ap, 0, 4, id) == 0 && ph_dsk_app_frequenti(NULL, 3, 4, id) == 0 &&
             ph_dsk_app_frequenti(ap, 6, 0, id) == 0);
    VERIFICA(ph_dsk_app_frequenti_max(PH_CFG_WIDGET_PICCOLO) == 4 && ph_dsk_app_frequenti_max(PH_CFG_WIDGET_MEDIO) == 8 &&
             ph_dsk_app_frequenti_max(PH_CFG_WIDGET_GRANDE) == 12);
    VERIFICA(ph_dsk_trasferimenti_max(PH_CFG_WIDGET_PICCOLO) == 3 && ph_dsk_trasferimenti_max(PH_CFG_WIDGET_MEDIO) == 5 &&
             ph_dsk_trasferimenti_max(PH_CFG_WIDGET_GRANDE) == 8);
    /* Piu' voci del profilo di quante ne contiene il tetto: nessuna uscita dal buffer. */
    PhCfgApertura molte[PH_CFG_MAX_APERTURE];
    memset(molte, 0, sizeof molte);
    for (size_t i = 0; i < PH_CFG_MAX_APERTURE; i++) {
        snprintf(molte[i].app, sizeof molte[i].app, "p%zu", i);
        molte[i].volte = (uint32_t)(i % 7 + 1);
        molte[i].ultima = (int64_t)i;
    }
    size_t grande[12];
    k = ph_dsk_app_frequenti(molte, PH_CFG_MAX_APERTURE, 12, grande);
    VERIFICA(k == 12);
    bool ordine = true;
    for (size_t i = 1; i < k; i++)
        if (molte[grande[i - 1]].volte < molte[grande[i]].volte)
            ordine = false;
    VERIFICA(ordine);

    /* Nomi di file: ostili, ripuliti. */
    char nm[64];
    ph_dsk_nome_sicuro("foto estate.jpg", nm, sizeof nm);
    VERIFICA_STR(nm, "foto estate.jpg");
    ph_dsk_nome_sicuro("a\nb\tc\x1b[31m.txt", nm, sizeof nm);
    VERIFICA(strchr(nm, '\n') == NULL && strchr(nm, '\t') == NULL && strchr(nm, '\x1b') == NULL);
    ph_dsk_nome_sicuro("file\xE2\x80\xAEgpj.exe", nm, sizeof nm); /* inversione della direzione: via */
    VERIFICA_STR(nm, "filegpj.exe");
    ph_dsk_nome_sicuro("\xff\xfe", nm, sizeof nm);
    VERIFICA(g_utf8_validate(nm, -1, NULL));
    ph_dsk_nome_sicuro(NULL, nm, sizeof nm);
    VERIFICA_STR(nm, "");
    ph_dsk_nome_sicuro("perch\xC3\xA9-lungo-lungo", nm, 8); /* taglio su un confine di carattere */
    VERIFICA(g_utf8_validate(nm, -1, NULL) && strlen(nm) < 8);
    ph_dsk_nome_sicuro("x", nm, 1);
    VERIFICA_STR(nm, "");
}

static void prova_meteo(void)
{
    ph_lingua_imposta(PH_LINGUA_IT);
    char o[200];
    /* Icone: gruppi dei codici WMO. */
    VERIFICA(ph_dsk_meteo_gruppo(0) == PH_DSK_COND_SERENO);
    VERIFICA(ph_dsk_meteo_gruppo(1) == PH_DSK_COND_POCO_NUVOLOSO && ph_dsk_meteo_gruppo(2) == PH_DSK_COND_POCO_NUVOLOSO);
    VERIFICA(ph_dsk_meteo_gruppo(3) == PH_DSK_COND_NUVOLOSO);
    VERIFICA(ph_dsk_meteo_gruppo(45) == PH_DSK_COND_NEBBIA && ph_dsk_meteo_gruppo(48) == PH_DSK_COND_NEBBIA);
    for (int c = 51; c <= 57; c++)
        VERIFICA(ph_dsk_meteo_gruppo(c) == PH_DSK_COND_PIOGGERELLA);
    for (int c = 61; c <= 67; c++)
        VERIFICA(ph_dsk_meteo_gruppo(c) == PH_DSK_COND_PIOGGIA);
    for (int c = 80; c <= 82; c++)
        VERIFICA(ph_dsk_meteo_gruppo(c) == PH_DSK_COND_PIOGGIA);
    for (int c = 71; c <= 77; c++)
        VERIFICA(ph_dsk_meteo_gruppo(c) == PH_DSK_COND_NEVE);
    VERIFICA(ph_dsk_meteo_gruppo(85) == PH_DSK_COND_NEVE && ph_dsk_meteo_gruppo(86) == PH_DSK_COND_NEVE);
    for (int c = 95; c <= 99; c++)
        VERIFICA(ph_dsk_meteo_gruppo(c) == PH_DSK_COND_TEMPORALE);
    /* Codici ignoti: nessuna icona. */
    VERIFICA(ph_dsk_meteo_gruppo(-1) == PH_DSK_COND_NESSUNA && ph_dsk_meteo_gruppo(4) == PH_DSK_COND_NESSUNA &&
             ph_dsk_meteo_gruppo(58) == PH_DSK_COND_NESSUNA && ph_dsk_meteo_gruppo(70) == PH_DSK_COND_NESSUNA &&
             ph_dsk_meteo_gruppo(83) == PH_DSK_COND_NESSUNA && ph_dsk_meteo_gruppo(100) == PH_DSK_COND_NESSUNA &&
             ph_dsk_meteo_gruppo(1000000) == PH_DSK_COND_NESSUNA);
    /* Ogni codice con icona ha un testo; i codici senza icona di tipo ignoto no. */
    for (int c = 0; c < 100; c++)
        if (ph_dsk_meteo_condizione(c)[0] != '\0')
            VERIFICA(ph_dsk_meteo_gruppo(c) != PH_DSK_COND_NESSUNA);
    VERIFICA_STR(ph_dsk_meteo_condizione(0), "Sereno");
    VERIFICA_STR(ph_dsk_meteo_condizione(61), "Pioggia debole");
    VERIFICA_STR(ph_dsk_meteo_condizione(96), "Temporale con grandine");
    VERIFICA_STR(ph_dsk_meteo_condizione(-1), "");
    VERIFICA_STR(ph_dsk_meteo_condizione(4), "");
    ph_lingua_imposta(PH_LINGUA_EN);
    VERIFICA_STR(ph_dsk_meteo_condizione(2), "Partly cloudy");
    ph_lingua_imposta(PH_LINGUA_IT);

    /* Valori: non noto = trattino lungo, mai numeri inventati. */
    const char *tr = "\xE2\x80\x94";
    ph_dsk_meteo_temp(14.4, o, sizeof o);
    VERIFICA_STR(o, "14\xC2\xB0");
    ph_dsk_meteo_temp(14.5, o, sizeof o);
    VERIFICA_STR(o, "15\xC2\xB0");
    ph_dsk_meteo_temp(-0.4, o, sizeof o);
    VERIFICA_STR(o, "0\xC2\xB0"); /* mai «-0°» */
    ph_dsk_meteo_temp(-2.6, o, sizeof o);
    VERIFICA_STR(o, "-3\xC2\xB0");
    ph_dsk_meteo_temp(NAN, o, sizeof o);
    VERIFICA_STR(o, tr);
    ph_dsk_meteo_temp(INFINITY, o, sizeof o);
    VERIFICA_STR(o, tr);
    ph_dsk_meteo_temp(9999, o, sizeof o);
    VERIFICA_STR(o, tr); /* valore assurdo */
    ph_dsk_meteo_temp_c(14.54, o, sizeof o);
    VERIFICA_STR(o, "14,5 \xC2\xB0" "C");
    ph_dsk_meteo_temp_c(-0.04, o, sizeof o);
    VERIFICA_STR(o, "0,0 \xC2\xB0" "C");
    ph_dsk_meteo_temp_c(-3.25, o, sizeof o);
    VERIFICA_STR(o, "-3,3 \xC2\xB0" "C");
    ph_dsk_meteo_temp_c(NAN, o, sizeof o);
    VERIFICA_STR(o, tr);
    ph_lingua_imposta(PH_LINGUA_EN);
    ph_dsk_meteo_temp_c(14.54, o, sizeof o);
    VERIFICA_STR(o, "14.5 \xC2\xB0" "C");
    ph_lingua_imposta(PH_LINGUA_IT);
    ph_dsk_meteo_vento(12.4, o, sizeof o);
    VERIFICA_STR(o, "12 km/h");
    ph_dsk_meteo_vento(NAN, o, sizeof o);
    VERIFICA_STR(o, tr);
    ph_dsk_meteo_vento(-5, o, sizeof o);
    VERIFICA_STR(o, tr);
    ph_dsk_meteo_percento(63.4, o, sizeof o);
    VERIFICA_STR(o, "63 %");
    ph_dsk_meteo_percento(140, o, sizeof o);
    VERIFICA_STR(o, "100 %");
    ph_dsk_meteo_percento(-3, o, sizeof o);
    VERIFICA_STR(o, "0 %");
    ph_dsk_meteo_percento(NAN, o, sizeof o);
    VERIFICA_STR(o, tr);
    ph_dsk_meteo_mm(0.44, o, sizeof o);
    VERIFICA_STR(o, "0,4 mm");
    ph_dsk_meteo_mm(NAN, o, sizeof o);
    VERIFICA_STR(o, tr);
    ph_dsk_meteo_temp(14, o, 3); /* buffer piccolo: nessuna uscita dal buffer */
    VERIFICA(strlen(o) < 3);

    /* Titolo. */
    ph_dsk_meteo_titolo("Roma", o, sizeof o);
    VERIFICA_STR(o, "Meteo \xE2\x80\x94 Roma");
    ph_dsk_meteo_titolo("", o, sizeof o);
    VERIFICA_STR(o, "Meteo");
    ph_dsk_meteo_titolo(NULL, o, sizeof o);
    VERIFICA_STR(o, "Meteo");
    ph_lingua_imposta(PH_LINGUA_EN);
    ph_dsk_meteo_titolo("Rome", o, sizeof o);
    VERIFICA_STR(o, "Weather \xE2\x80\x94 Rome");
    ph_lingua_imposta(PH_LINGUA_IT);

    /* Ore nel fuso del luogo (UTC+2: 2026-11-23 10:00Z = 12:00). */
    ph_dsk_meteo_ora_luogo(1795428000, 7200, o, sizeof o);
    VERIFICA_STR(o, "12:00");
    ph_dsk_meteo_ora_luogo(1795428000, -5 * 3600, o, sizeof o);
    VERIFICA_STR(o, "05:00");
    ph_dsk_meteo_ora_luogo(1795428000 + 30 * 60, 3600 + 1800, o, sizeof o); /* fusi a mezz'ora */
    VERIFICA_STR(o, "12:00");
    ph_dsk_meteo_ora_luogo(0, -3600, o, sizeof o); /* prima del 1970 locale: 23:00 */
    VERIFICA_STR(o, "23:00");
    ph_dsk_meteo_ora_luogo(-5, 0, o, sizeof o);
    VERIFICA_STR(o, tr);
    ph_dsk_meteo_ora_luogo(100, 999999, o, sizeof o); /* scarto assurdo */
    VERIFICA_STR(o, tr);
    ph_dsk_meteo_ora_breve(1795428000, 7200, o, sizeof o);
    VERIFICA_STR(o, "12");
    /* Giorni: «Oggi» e poi il giorno abbreviato; il giorno e' la mezzanotte locale in secondi Unix. */
    int64_t oggi = 1795428000 / 86400 * 86400 - 7200; /* mezzanotte locale (UTC+2) del 23 novembre */
    ph_dsk_meteo_giorno_nome(oggi, oggi, 7200, o, sizeof o);
    VERIFICA_STR(o, "Oggi");
    ph_dsk_meteo_giorno_nome(oggi + 86400, oggi, 7200, o, sizeof o);
    VERIFICA_STR(o, "Mar"); /* 24 novembre 2026 e' un martedi' */
    ph_dsk_meteo_giorno_nome(oggi + 6 * 86400, oggi, 7200, o, sizeof o);
    VERIFICA_STR(o, "Dom");

    /* Quanto mostrare per stile e misura. */
    VERIFICA(ph_dsk_meteo_n_ore(PH_DSK_METEO_ORE, PH_CFG_WIDGET_PICCOLO) == 4 &&
             ph_dsk_meteo_n_ore(PH_DSK_METEO_ORE, PH_CFG_WIDGET_MEDIO) == 5 &&
             ph_dsk_meteo_n_ore(PH_DSK_METEO_ORE, PH_CFG_WIDGET_GRANDE) == 7);
    VERIFICA(ph_dsk_meteo_n_ore(PH_DSK_METEO_CURVA, PH_CFG_WIDGET_GRANDE) == 24 &&
             ph_dsk_meteo_n_ore(PH_DSK_METEO_CURVA, PH_CFG_WIDGET_MEDIO) == 12);
    VERIFICA(ph_dsk_meteo_n_ore(PH_DSK_METEO_OGGI, PH_CFG_WIDGET_GRANDE) == 6 &&
             ph_dsk_meteo_n_ore(PH_DSK_METEO_OGGI, PH_CFG_WIDGET_MEDIO) == 0);
    VERIFICA(ph_dsk_meteo_n_ore(PH_DSK_METEO_ADESSO, PH_CFG_WIDGET_GRANDE) == 0 &&
             ph_dsk_meteo_n_ore(PH_DSK_METEO_COMPATTO, PH_CFG_WIDGET_GRANDE) == 0);
    VERIFICA(ph_dsk_meteo_n_giorni(PH_DSK_METEO_SETTIMANA, PH_CFG_WIDGET_PICCOLO) == 3 &&
             ph_dsk_meteo_n_giorni(PH_DSK_METEO_SETTIMANA, PH_CFG_WIDGET_MEDIO) == 5 &&
             ph_dsk_meteo_n_giorni(PH_DSK_METEO_SETTIMANA, PH_CFG_WIDGET_GRANDE) == 7 &&
             ph_dsk_meteo_n_giorni(PH_DSK_METEO_ORE, PH_CFG_WIDGET_GRANDE) == 0);
    for (int dd = 0; dd < 3; dd++) { /* mai oltre quanto il contratto dispone */
        VERIFICA(ph_dsk_meteo_n_ore(PH_DSK_METEO_CURVA, (PhCfgWidgetDim)dd) <= PH_METEO_ORE_MAX);
        VERIFICA(ph_dsk_meteo_n_giorni(PH_DSK_METEO_SETTIMANA, (PhCfgWidgetDim)dd) <= PH_METEO_GIORNI_MAX);
    }
    VERIFICA(ph_dsk_meteo_n_dettagli(PH_CFG_WIDGET_PICCOLO) == 2 && ph_dsk_meteo_n_dettagli(PH_CFG_WIDGET_GRANDE) == 4);
    VERIFICA(ph_dsk_stili_n(PH_CFG_WIDGET_METEO) == PH_DSK_METEO_STILI && PH_DSK_METEO_STILI == 6);
    VERIFICA_STR(ph_dsk_stile_nome_di(PH_CFG_WIDGET_METEO, PH_DSK_METEO_ORE), "Ore");
    VERIFICA(ph_dsk_stile_nome_di(PH_CFG_WIDGET_METEO, 6) == NULL);

    /* Barre della settimana. */
    PhMeteoGiorno gg[3];
    memset(gg, 0, sizeof gg);
    for (int i = 0; i < 3; i++)
        gg[i].minima = gg[i].massima = NAN;
    double gmin = 0, gmax = 0, da, a;
    VERIFICA(!ph_dsk_meteo_estremi(gg, 3, &gmin, &gmax)); /* nessun valore noto */
    gg[0].minima = -2;
    gg[0].massima = 6;
    gg[1].minima = 4;
    gg[1].massima = 18;
    VERIFICA(ph_dsk_meteo_estremi(gg, 3, &gmin, &gmax) && gmin == -2 && gmax == 18);
    VERIFICA(ph_dsk_meteo_barra(-2, 6, gmin, gmax, &da, &a) && vicino((float)da, 0) && vicino((float)a, 0.4f));
    VERIFICA(ph_dsk_meteo_barra(4, 18, gmin, gmax, &da, &a) && vicino((float)da, 0.3f) && vicino((float)a, 1.0f));
    VERIFICA(ph_dsk_meteo_barra(5, 5, gmin, gmax, &da, &a) && a - da >= 0.0999 && da >= 0 && a <= 1); /* larghezza minima */
    VERIFICA(ph_dsk_meteo_barra(18, 18, gmin, gmax, &da, &a) && a <= 1.0 && da >= 0.89); /* contro il bordo */
    VERIFICA(!ph_dsk_meteo_barra(NAN, 3, gmin, gmax, &da, &a) && !ph_dsk_meteo_barra(3, NAN, gmin, gmax, &da, &a));
    VERIFICA(ph_dsk_meteo_barra(3, 3, 3, 3, &da, &a) && da == 0 && a == 1); /* tutti uguali */
    VERIFICA(ph_dsk_meteo_barra(8, 2, gmin, gmax, &da, &a) && da < a); /* massima < minima: si scambiano */

    /* Stati. */
    PhDskMeteoStato s;
    int64_t rx = 1795428000;
    ph_dsk_meteo_stato(PH_SRV_METEO_PRONTO, true, "", rx, 7200, false, &s);
    VERIFICA(s.mostra_dati && !s.grigio && s.messaggio[0] == '\0' && !s.invito_localita);
    ph_dsk_meteo_stato(PH_SRV_METEO_PRONTO, false, "", 0, 0, false, &s); /* pronto ma senza dati: non si inventa */
    VERIFICA(!s.mostra_dati);
    VERIFICA_STR(s.messaggio, "Previsioni non disponibili");
    ph_dsk_meteo_stato(PH_SRV_METEO_IN_ATTESA, false, "", 0, 0, false, &s);
    VERIFICA(!s.mostra_dati);
    VERIFICA_STR(s.messaggio, "Aggiornamento\xE2\x80\xA6");
    ph_dsk_meteo_stato(PH_SRV_METEO_IN_ATTESA, true, "", rx, 7200, false, &s);
    VERIFICA(s.mostra_dati && !s.grigio && s.messaggio[0] == '\0');
    ph_dsk_meteo_stato(PH_SRV_METEO_NON_DISPONIBILE, true, "", rx, 7200, false, &s);
    VERIFICA(s.mostra_dati && s.grigio);
    VERIFICA_STR(s.messaggio, "Previsioni non disponibili");
    VERIFICA_STR(s.dettaglio, "ultimo dato alle 12:00");
    ph_dsk_meteo_stato(PH_SRV_METEO_NON_DISPONIBILE, false, "", 0, 0, false, &s);
    VERIFICA(!s.mostra_dati && s.dettaglio[0] == '\0');
    ph_dsk_meteo_stato(PH_SRV_METEO_SENZA_POSIZIONE, false, "Posizione spenta sul telefono", 0, 0, false, &s);
    VERIFICA_STR(s.messaggio, "Posizione spenta sul telefono");
    VERIFICA(s.invito_localita && !s.mostra_dati);
    ph_dsk_meteo_stato(PH_SRV_METEO_SENZA_POSIZIONE, false, "", 0, 0, false, &s);
    VERIFICA_STR(s.messaggio, "Il telefono non d\xC3\xA0 la sua posizione");
    ph_dsk_meteo_stato(PH_SRV_METEO_SENZA_POSIZIONE, false, "x", 0, 0, true, &s);
    VERIFICA(!s.invito_localita); /* una citta' e' gia' scelta: niente invito */
    ph_dsk_meteo_stato(PH_SRV_METEO_NON_COLLEGATO, true, "", rx, 7200, false, &s);
    VERIFICA_STR(s.messaggio, "Telefono non collegato");
    VERIFICA(s.mostra_dati && s.grigio && s.invito_localita);
    VERIFICA_STR(s.dettaglio, "ultimo dato alle 12:00");
    ph_dsk_meteo_stato(PH_SRV_METEO_SPENTO, false, "", 0, 0, false, &s);
    VERIFICA(!s.mostra_dati);
    ph_lingua_imposta(PH_LINGUA_EN);
    ph_dsk_meteo_stato(PH_SRV_METEO_NON_DISPONIBILE, true, "", rx, 7200, false, &s);
    VERIFICA_STR(s.messaggio, "Forecast not available");
    VERIFICA_STR(s.dettaglio, "last data at 12:00");
    ph_lingua_imposta(PH_LINGUA_IT);

    /* Valori della curva: noti ed entro +-150, altrimenti non noti. */
    VERIFICA(ph_dsk_meteo_valore_valido(0) && ph_dsk_meteo_valore_valido(-3.5) && ph_dsk_meteo_valore_valido(150) &&
             ph_dsk_meteo_valore_valido(-150));
    VERIFICA(!ph_dsk_meteo_valore_valido(150.1) && !ph_dsk_meteo_valore_valido(-9999) && !ph_dsk_meteo_valore_valido(NAN) &&
             !ph_dsk_meteo_valore_valido(INFINITY) && !ph_dsk_meteo_valore_valido(-INFINITY));

    /* Citta' scelta a mano nel profilo. */
    char lungo[400];
    memset(lungo, 'a', sizeof lungo - 1);
    lungo[sizeof lungo - 1] = '\0';
    char tr2[128];
    ph_dsk_tronca_utf8(lungo, tr2, sizeof tr2);
    VERIFICA(strlen(tr2) == 127);
    /* Multibyte a cavallo del limite (127 byte al massimo). 126 'a' + «è» (2 byte): la «è» finirebbe al
     * byte 128, non sta: si arretra a 126. */
    memset(lungo, 'a', 126);
    strcpy(lungo + 126, "\xC3\xA8" "zzzz");
    ph_dsk_tronca_utf8(lungo, tr2, sizeof tr2);
    VERIFICA(strlen(tr2) == 126 && g_utf8_validate(tr2, -1, NULL));
    memset(lungo, 'a', 125); /* 125 'a' + «è»: sta (127 byte) */
    strcpy(lungo + 125, "\xC3\xA8" "zzzz");
    ph_dsk_tronca_utf8(lungo, tr2, sizeof tr2);
    VERIFICA(strlen(tr2) == 127 && g_utf8_validate(tr2, -1, NULL));
    memset(lungo, 'a', 127);
    strcpy(lungo + 127, "\xC3\xA8" "zzzz");
    ph_dsk_tronca_utf8(lungo, tr2, sizeof tr2);
    VERIFICA(strlen(tr2) == 127 && g_utf8_validate(tr2, -1, NULL)); /* il carattere a meta' non c'e' */
    memset(lungo, 'a', 124);
    strcpy(lungo + 124, "\xE2\x82\xAC" "zzzz"); /* euro a 3 byte: 124-126 sta, 127 byte in tutto */
    ph_dsk_tronca_utf8(lungo, tr2, sizeof tr2);
    VERIFICA(strlen(tr2) == 127 && g_utf8_validate(tr2, -1, NULL));
    memset(lungo, 'a', 126);
    strcpy(lungo + 126, "\xE2\x82\xAC" "zzzz"); /* l'euro finirebbe al byte 129: si arretra a 126 */
    ph_dsk_tronca_utf8(lungo, tr2, sizeof tr2);
    VERIFICA(strlen(tr2) == 126 && g_utf8_validate(tr2, -1, NULL));
    ph_dsk_tronca_utf8("\xff\xfe" "abc", tr2, sizeof tr2); /* non UTF-8: nulla di invalido esce */
    VERIFICA(g_utf8_validate(tr2, -1, NULL));
    ph_dsk_tronca_utf8(NULL, tr2, sizeof tr2);
    VERIFICA_STR(tr2, "");
    ph_dsk_tronca_utf8("abcdef", tr2, 1);
    VERIFICA_STR(tr2, "");

    PhCfgTelefono *p = ph_cfg_telefono_nuovo();
    if (p != NULL) {
        VERIFICA(!p->meteo_luogo_scelto);
        VERIFICA(!ph_dsk_luogo_scrivi(p, "", "x", 4000, 1000) && !p->meteo_luogo_scelto); /* nome vuoto */
        VERIFICA(!ph_dsk_luogo_scrivi(p, "Roma", "x", 9001, 0) && !ph_dsk_luogo_scrivi(p, "Roma", "x", 0, -18001));
        VERIFICA(!p->meteo_luogo_scelto);
        VERIFICA(ph_dsk_luogo_scrivi(p, "Roma", "Lazio, Italia", 4189, 1248));
        VERIFICA(p->meteo_luogo_scelto && strcmp(p->meteo_luogo_nome, "Roma") == 0 &&
                 strcmp(p->meteo_luogo_dettaglio, "Lazio, Italia") == 0 && p->meteo_luogo_lat_centesimi == 4189 &&
                 p->meteo_luogo_lon_centesimi == 1248);
        ph_dsk_luogo_azzera(p);
        VERIFICA(!p->meteo_luogo_scelto && p->meteo_luogo_nome[0] == '\0' && p->meteo_luogo_dettaglio[0] == '\0' &&
                 p->meteo_luogo_lat_centesimi == 0 && p->meteo_luogo_lon_centesimi == 0);
        /* Nome da 200 byte (dato ostile): troncato sotto i 128, il profilo resta scrivibile. */
        memset(lungo, 'b', 200);
        lungo[200] = '\0';
        VERIFICA(ph_dsk_luogo_scrivi(p, lungo, lungo, 0, 0) && strlen(p->meteo_luogo_nome) <= 127 &&
                 strlen(p->meteo_luogo_dettaglio) <= 127);
        ph_cfg_telefono_libera(p);
    }
}

/* ---- Componenti di disegno: geometria pura (logica_comp.c); i widget (componenti.c) si provano
 * a schermo con la galleria del demo (test shell-desktop-componenti) ---- */
static bool vicino_d(double a, double b)
{
    return fabs(a - b) < 1e-6;
}

static void prova_componenti_anello(void)
{
    VERIFICA(ph_dsk_percento_valido(78.4) == 78 && ph_dsk_percento_valido(99.5) == 100);
    VERIFICA(ph_dsk_percento_valido(-5) == 0 && ph_dsk_percento_valido(250) == 100);
    VERIFICA(ph_dsk_percento_valido(NAN) == 0 && ph_dsk_percento_valido(INFINITY) == 0);
    double da, a;
    ph_dsk_anello_angoli(0, &da, &a);
    VERIFICA(vicino_d(da, -G_PI / 2) && vicino_d(a, da)); /* vuoto */
    ph_dsk_anello_angoli(50, &da, &a);
    VERIFICA(vicino_d(a - da, G_PI));
    ph_dsk_anello_angoli(100, &da, &a);
    VERIFICA(vicino_d(a - da, 2 * G_PI));
    ph_dsk_anello_angoli(1e9, &da, &a); /* oltre il cento: il cerchio e basta */
    VERIFICA(vicino_d(a - da, 2 * G_PI));
    ph_dsk_anello_angoli(-30, &da, &a);
    VERIFICA(vicino_d(a, da));
    ph_dsk_anello_angoli(NAN, &da, &a);
    VERIFICA(vicino_d(a, da));
    double r, sp;
    ph_dsk_anello_geometria(96, &r, &sp);
    VERIFICA(sp > 8.9 && sp < 9.1 && vicino_d(r, (96 - sp) / 2));
    ph_dsk_anello_geometria(1000, &r, &sp);
    VERIFICA(vicino_d(sp, 14));
    ph_dsk_anello_geometria(10, &r, &sp); /* lato piccolo: il tratto non supera un quarto */
    VERIFICA(vicino_d(sp, 2.5) && r + sp / 2 <= 5.0 + 1e-9);
    const double ostili[] = {0, -1, NAN, INFINITY, -INFINITY};
    for (size_t i = 0; i < G_N_ELEMENTS(ostili); i++) {
        ph_dsk_anello_geometria(ostili[i], &r, &sp);
        VERIFICA(r == 0 && sp == 0);
    }
}

static void prova_componenti_fette(void)
{
    int b[PH_DSK_FETTE_MAX + 1];
    const int64_t v[5] = {21, 34, 3, 18, 10};
    VERIFICA(ph_dsk_fette_bordi(v, 5, 128, 320, b) == 5);
    VERIFICA(b[0] == 0 && b[5] == (int)floor(86.0 / 128.0 * 320.0)); /* il resto e' il libero */
    for (int i = 0; i < 5; i++)
        VERIFICA(b[i + 1] >= b[i]);
    /* Totale zero, negativo o minore della somma: contano le fette. */
    const int64_t due[2] = {10, 10};
    ph_dsk_fette_bordi(due, 2, 0, 100, b);
    VERIFICA(b[0] == 0 && b[1] == 50 && b[2] == 100);
    ph_dsk_fette_bordi(due, 2, -7, 100, b);
    VERIFICA(b[2] == 100);
    const int64_t grandi[2] = {80, 80};
    ph_dsk_fette_bordi(grandi, 2, 100, 100, b); /* somma 160 su totale 100 */
    VERIFICA(b[1] == 50 && b[2] == 100);
    /* Negativi: valgono zero. */
    const int64_t neg[3] = {-5, 10, INT64_MIN};
    ph_dsk_fette_bordi(neg, 3, 20, 100, b);
    VERIFICA(b[0] == 0 && b[1] == 0 && b[2] == 50 && b[3] == 50);
    /* Tutti zero, larghezza non positiva, nessun array. */
    const int64_t zeri[3] = {0, 0, 0};
    VERIFICA(ph_dsk_fette_bordi(zeri, 3, 100, 100, b) == 3 && b[0] + b[1] + b[2] + b[3] == 0);
    VERIFICA(ph_dsk_fette_bordi(due, 2, 100, 0, b) == 2 && b[2] == 0);
    VERIFICA(ph_dsk_fette_bordi(due, 2, 100, -50, b) == 2 && b[2] == 0);
    VERIFICA(ph_dsk_fette_bordi(NULL, 4, 100, 100, b) == 0 && b[0] == 0);
    VERIFICA(ph_dsk_fette_bordi(due, 0, 100, 100, b) == 0);
    /* Piu' fette del tetto: se ne usano PH_DSK_FETTE_MAX e il buffer non si supera. */
    int64_t tante[20];
    for (int i = 0; i < 20; i++)
        tante[i] = 1;
    int grande[40];
    memset(grande, 0x7f, sizeof grande);
    VERIFICA(ph_dsk_fette_bordi(tante, 20, 20, 160, grande) == PH_DSK_FETTE_MAX);
    VERIFICA(grande[PH_DSK_FETTE_MAX + 1] == 0x7f7f7f7f && grande[PH_DSK_FETTE_MAX] == 64);
    /* Valori enormi: nessun trabocco. */
    const int64_t enormi[2] = {INT64_MAX, INT64_MAX};
    ph_dsk_fette_bordi(enormi, 2, 5, 200, b);
    VERIFICA(b[1] == 100 && b[2] == 200);
    /* Proprieta': su valori qualunque i bordi non calano e l'ultimo non supera la larghezza. */
    uint64_t seme = 12345;
    for (int giro = 0; giro < 500; giro++) {
        int64_t q[PH_DSK_FETTE_MAX];
        for (int i = 0; i < PH_DSK_FETTE_MAX; i++) {
            seme = seme * 6364136223846793005ULL + 1442695040888963407ULL;
            q[i] = (int64_t)(seme >> (giro % 60)) * ((seme & 1) ? 1 : -1);
        }
        seme = seme * 6364136223846793005ULL + 1442695040888963407ULL;
        int64_t tot = (int64_t)(seme >> 20) - (1 << 20);
        int larg = (int)(seme % 700);
        int bo[PH_DSK_FETTE_MAX + 1];
        size_t n = ph_dsk_fette_bordi(q, PH_DSK_FETTE_MAX, tot, larg, bo);
        VERIFICA(n == PH_DSK_FETTE_MAX && bo[0] == 0 && bo[n] <= larg);
        for (size_t i = 0; i < n; i++)
            VERIFICA(bo[i + 1] >= bo[i]);
    }
}

static void prova_componenti_curva(void)
{
    PhDskCurvaPunti P;
    const double t[7] = {21, 21, 20, 18, 16, 15, 14};
    ph_dsk_curva_punti(t, 7, 300, 10, 20, 80, &P);
    VERIFICA(P.n == 7 && P.imax == 0 && P.imin == 6 && P.tmax == 21 && P.tmin == 14);
    VERIFICA(vicino_d(P.x[0], 10) && vicino_d(P.x[6], 290));
    VERIFICA(vicino_d(P.y[0], 20) && vicino_d(P.y[6], 80)); /* massima in alto, minima in basso */
    for (size_t i = 1; i < P.n; i++)
        VERIFICA(P.x[i] > P.x[i - 1]);
    /* Nessun punto, un punto. */
    ph_dsk_curva_punti(t, 0, 300, 10, 20, 80, &P);
    VERIFICA(P.n == 0);
    ph_dsk_curva_punti(NULL, 5, 300, 10, 20, 80, &P);
    VERIFICA(P.n == 0);
    ph_dsk_curva_punti(t, 1, 300, 10, 20, 80, &P);
    VERIFICA(P.n == 1 && vicino_d(P.x[0], 150) && vicino_d(P.y[0], 50));
    /* Minimi = massimi: linea a meta' altezza. */
    const double piatta[4] = {18, 18, 18, 18};
    ph_dsk_curva_punti(piatta, 4, 300, 10, 20, 80, &P);
    VERIFICA(P.n == 4 && P.imin == 0 && P.imax == 0);
    for (size_t i = 0; i < 4; i++)
        VERIFICA(vicino_d(P.y[i], 50));
    /* Escursione sotto il minimo: non si ingrandisce il rumore. */
    const double quasi[2] = {20, 20.5};
    ph_dsk_curva_punti(quasi, 2, 100, 0, 0, 100, &P);
    VERIFICA(P.y[0] > 25 && P.y[1] < 75 && P.y[0] > P.y[1]);
    /* Valori non finiti: la media degli altri; sempre dentro le ordinate (anche scambiate). */
    const double sporca[4] = {10, NAN, INFINITY, 20};
    ph_dsk_curva_punti(sporca, 4, 200, 0, 90, 10, &P);
    VERIFICA(P.n == 4 && vicino_d(P.tmin, 10) && vicino_d(P.tmax, 20));
    for (size_t i = 0; i < P.n; i++)
        VERIFICA(P.y[i] >= 10 && P.y[i] <= 90 && isfinite(P.x[i]));
    const double nulla[2] = {NAN, NAN};
    ph_dsk_curva_punti(nulla, 2, 200, 0, 0, 100, &P);
    VERIFICA(P.n == 2 && isfinite(P.y[0]) && isfinite(P.y[1]));
    /* Larghezza o margini assurdi, troppi punti. */
    ph_dsk_curva_punti(t, 7, -50, 500, NAN, INFINITY, &P);
    VERIFICA(P.n == 7);
    for (size_t i = 0; i < P.n; i++)
        VERIFICA(isfinite(P.x[i]) && isfinite(P.y[i]));
    double molti[200];
    for (int i = 0; i < 200; i++)
        molti[i] = i;
    ph_dsk_curva_punti(molti, 200, 300, 0, 0, 50, &P);
    VERIFICA(P.n == PH_DSK_CURVA_MAX);
    /* Passo delle etichette. */
    VERIFICA(ph_dsk_curva_passo_etichette(0, 300, 20) == 1 && ph_dsk_curva_passo_etichette(1, 300, 20) == 1);
    VERIFICA(ph_dsk_curva_passo_etichette(12, 0, 20) == 12 && ph_dsk_curva_passo_etichette(12, NAN, 20) == 12);
    VERIFICA(ph_dsk_curva_passo_etichette(12, 300, 0) == 1 && ph_dsk_curva_passo_etichette(12, 300, NAN) == 1);
    VERIFICA(ph_dsk_curva_passo_etichette(7, 1000, 20) == 1);
    size_t passo = ph_dsk_curva_passo_etichette(12, 300, 24);
    VERIFICA(passo == 2 && (double)passo * 300.0 / 11.0 >= 24.0);
    VERIFICA(ph_dsk_curva_passo_etichette(12, 5, 24) == 12); /* mai oltre n */
}

static void prova_componenti_tratto(void)
{
    double x0, x1;
    VERIFICA(ph_dsk_tratto_px(12, 22, 8, 26, 180, 6, &x0, &x1) && vicino_d(x0, 40) && vicino_d(x1, 140));
    VERIFICA(ph_dsk_tratto_px(22, 12, 8, 26, 180, 6, &x0, &x1) && vicino_d(x0, 40) && vicino_d(x1, 140));
    VERIFICA(ph_dsk_tratto_px(12, 22, 26, 8, 180, 6, &x0, &x1) && vicino_d(x0, 40) && vicino_d(x1, 140));
    /* Fuori scala: si ritaglia. */
    VERIFICA(ph_dsk_tratto_px(-100, 100, 8, 26, 180, 6, &x0, &x1) && vicino_d(x0, 0) && vicino_d(x1, 180));
    /* Scala senza ampiezza: tutta la larghezza. */
    VERIFICA(ph_dsk_tratto_px(5, 5, 5, 5, 180, 6, &x0, &x1) && vicino_d(x0, 0) && vicino_d(x1, 180));
    /* Minima = massima: largo almeno il minimo, dentro la barra, anche ai bordi. */
    VERIFICA(ph_dsk_tratto_px(15, 15, 8, 26, 180, 6, &x0, &x1) && vicino_d(x1 - x0, 6) && x0 > 0 && x1 < 180);
    VERIFICA(ph_dsk_tratto_px(26, 26, 8, 26, 180, 6, &x0, &x1) && vicino_d(x1, 180) && vicino_d(x1 - x0, 6));
    VERIFICA(ph_dsk_tratto_px(8, 8, 8, 26, 180, 6, &x0, &x1) && vicino_d(x0, 0) && vicino_d(x1 - x0, 6));
    /* Minimo piu' largo della barra: tutta la barra. */
    VERIFICA(ph_dsk_tratto_px(10, 11, 8, 26, 4, 50, &x0, &x1) && vicino_d(x0, 0) && vicino_d(x1, 4));
    /* Valori non utilizzabili. */
    VERIFICA(!ph_dsk_tratto_px(NAN, 22, 8, 26, 180, 6, &x0, &x1) && x0 == 0 && x1 == 0);
    VERIFICA(!ph_dsk_tratto_px(12, 22, 8, INFINITY, 180, 6, &x0, &x1));
    VERIFICA(!ph_dsk_tratto_px(12, 22, 8, 26, 0, 6, &x0, &x1) && !ph_dsk_tratto_px(12, 22, 8, 26, -5, 6, &x0, &x1));
    VERIFICA(ph_dsk_tratto_px(12, 22, 8, 26, 180, NAN, &x0, &x1) && x1 > x0);
}

static void prova_componenti_testi(void)
{
    char s[16];
    ph_dsk_sigla("Alex", s, sizeof s);
    VERIFICA_STR(s, "A");
    ph_dsk_sigla("  mario", s, sizeof s);
    VERIFICA_STR(s, "M");
    ph_dsk_sigla("\xc3\xa9lan", s, sizeof s); /* é -> É */
    VERIFICA_STR(s, "\xc3\x89");
    ph_dsk_sigla("\xf0\x9f\x98\x80 ciao", s, sizeof s); /* un'emoji e' una lettera sola */
    VERIFICA_STR(s, "\xf0\x9f\x98\x80");
    ph_dsk_sigla("", s, sizeof s);
    VERIFICA_STR(s, "?");
    ph_dsk_sigla(NULL, s, sizeof s);
    VERIFICA_STR(s, "?");
    ph_dsk_sigla(" \t\n", s, sizeof s);
    VERIFICA_STR(s, "?");
    ph_dsk_sigla("\xff\xfe" "abc", s, sizeof s); /* UTF-8 non valido */
    VERIFICA_STR(s, "?");
    char piccolo[2] = {'x', 'x'};
    ph_dsk_sigla("\xf0\x9f\x98\x80", piccolo, 2); /* non ci sta: segnaposto, mai oltre il buffer */
    VERIFICA_STR(piccolo, "?");
    char uno[1] = {'x'};
    ph_dsk_sigla("Alex", uno, 1);
    VERIFICA(uno[0] == '\0');
    ph_dsk_sigla("Alex", s, 0); /* cap 0: non scrive */
    /* Numero grande. */
    VERIFICA(vicino_d(ph_dsk_numero_corpo(200, 60, 2, 12, 44), 44));
    VERIFICA(ph_dsk_numero_corpo(200, 60, 10, 12, 44) < 33 && ph_dsk_numero_corpo(200, 60, 10, 12, 44) > 32);
    VERIFICA(vicino_d(ph_dsk_numero_corpo(200, 30, 2, 12, 44), 30 / 1.15)); /* l'altezza e' il limite */
    VERIFICA(vicino_d(ph_dsk_numero_corpo(0, 60, 2, 12, 44), 12) && vicino_d(ph_dsk_numero_corpo(200, -1, 2, 12, 44), 12));
    VERIFICA(vicino_d(ph_dsk_numero_corpo(200, 60, 0, 12, 44), 12) && vicino_d(ph_dsk_numero_corpo(NAN, 60, 2, 12, 44), 12));
    VERIFICA(vicino_d(ph_dsk_numero_corpo(10, 10, 20, 12, 44), 12)); /* mai sotto il minimo */
    VERIFICA(vicino_d(ph_dsk_numero_corpo(200, 60, 2, 30, 20), 30)); /* massimo sotto il minimo: vale il minimo */
    /* Accenti. */
    VERIFICA(ph_dsk_accento_valido(-1, PH_DSK_ACC_N) == 0 && ph_dsk_accento_valido(3, PH_DSK_ACC_N) == 3);
    VERIFICA(ph_dsk_accento_valido(PH_DSK_ACC_N, PH_DSK_ACC_N) == 0 && ph_dsk_accento_valido(2, 0) == 0);
}

/* Valori finiti ma enormi (1e308, DBL_MAX): niente inf, niente NaN, nulla fuori dalle ordinate. */
static void prova_componenti_enormi(void)
{
    PhDskCurvaPunti P;
    const double s1[2] = {-1e308, 1e308};
    ph_dsk_curva_punti(s1, 2, 300, 10, 20, 80, &P);
    for (size_t i = 0; i < P.n; i++)
        VERIFICA(isfinite(P.x[i]) && isfinite(P.y[i]) && P.y[i] >= 20 && P.y[i] <= 80);
    /* Valori precisi: senza il ritaglio la differenza e' inf e il massimo cadrebbe a meta' (ultima difesa). */
    VERIFICA(P.n == 2 && isfinite(P.tmin) && isfinite(P.tmax) && vicino_d(P.y[1], 20) && vicino_d(P.y[0], 80));
    const double s2[3] = {1e308, 1e308, NAN};
    ph_dsk_curva_punti(s2, 3, 300, 10, 20, 80, &P);
    for (size_t i = 0; i < P.n; i++)
        VERIFICA(isfinite(P.x[i]) && isfinite(P.y[i]) && P.y[i] >= 20 && P.y[i] <= 80);
    const double s3[4] = {DBL_MAX, -DBL_MAX, DBL_MAX, DBL_MAX};
    ph_dsk_curva_punti(s3, 4, DBL_MAX, DBL_MAX, -DBL_MAX, DBL_MAX, &P);
    for (size_t i = 0; i < P.n; i++)
        VERIFICA(isfinite(P.x[i]) && isfinite(P.y[i]));
    double x0, x1;
    VERIFICA(ph_dsk_tratto_px(-1e308, 1e308, -1e308, 1e308, 180, 6, &x0, &x1) && isfinite(x0) && isfinite(x1) &&
             x0 >= 0 && x1 <= 180 && x1 >= x0);
    VERIFICA(ph_dsk_tratto_px(DBL_MAX, -DBL_MAX, -DBL_MAX, DBL_MAX, DBL_MAX, DBL_MAX, &x0, &x1) && isfinite(x0) &&
             isfinite(x1) && x0 <= x1);
    VERIFICA(ph_dsk_tratto_px(5, 6, -1e308, 1e308, 180, 6, &x0, &x1) && isfinite(x0) && isfinite(x1) && x1 - x0 >= 5.9);
    /* Disposizione adattiva. */
    VERIFICA(ph_dsk_legenda_colonne(160, 97, 14, 5) == 1);   /* due colonne da 73: il nome non ci sta */
    VERIFICA(ph_dsk_legenda_colonne(340, 97, 14, 5) == 2);
    VERIFICA(ph_dsk_legenda_colonne(208, 97, 14, 5) == 2);   /* esattamente al limite: (208-14)/2 = 97 */
    VERIFICA(ph_dsk_legenda_colonne(207, 97, 14, 5) == 1);
    VERIFICA(ph_dsk_legenda_colonne(340, 97, 14, 1) == 1 && ph_dsk_legenda_colonne(340, 97, 14, 0) == 1);
    VERIFICA(ph_dsk_legenda_colonne(0, 97, 14, 5) == 1 && ph_dsk_legenda_colonne(NAN, 97, 14, 5) == 1);
    VERIFICA(ph_dsk_legenda_colonne(340, NAN, 14, 5) == 1 && ph_dsk_legenda_colonne(-5, 97, 14, 5) == 1);
    VERIFICA(ph_dsk_riga_ora_sotto(148, 32, 130, 30, 10));   /* 32+10+130+10+30 = 212 > 148 */
    VERIFICA(!ph_dsk_riga_ora_sotto(340, 32, 130, 30, 10));
    VERIFICA(!ph_dsk_riga_ora_sotto(212, 32, 130, 30, 10) && ph_dsk_riga_ora_sotto(211, 32, 130, 30, 10));
    VERIFICA(!ph_dsk_riga_ora_sotto(148, 32, 130, 0, 10));   /* senza ora niente da spostare */
    VERIFICA(!ph_dsk_riga_ora_sotto(0, 32, 130, 30, 10) && !ph_dsk_riga_ora_sotto(NAN, 32, 130, 30, 10));
    VERIFICA(!ph_dsk_riga_ora_sotto(148, 32, NAN, 30, 10));
}

static void prova_predefiniti_distanziati(void)
{
    PhCfgWidget w[3] = {{.tipo = PH_CFG_WIDGET_OROLOGIO, .destra = 16, .alto = 16, .dimensione = PH_CFG_WIDGET_MEDIO},
                        {.tipo = PH_CFG_WIDGET_STATO, .destra = 16, .alto = 120, .dimensione = PH_CFG_WIDGET_MEDIO},
                        {.tipo = PH_CFG_WIDGET_NOTIFICHE, .destra = 16, .alto = 240, .dimensione = PH_CFG_WIDGET_MEDIO}};
    int alti[3] = {-7, -7, -7};
    VERIFICA(ph_dsk_predefiniti_alti(w, 3, alti) && alti[0] == 16);
    /* Tra un widget e il successivo ci sono esattamente PH_DSK_DISTANZA_WIDGET px (12). */
    for (size_t i = 0; i + 1 < 3; i++) {
        PhDskMisura m = ph_dsk_misura_da_profilo(w[i].tipo, 0, w[i].dimensione, 0, 0);
        VERIFICA(alti[i + 1] - (alti[i] + m.altezza) == 12);
    }
    /* Qualunque cambiamento dell'utente: il profilo non e' piu' quello di fabbrica, nulla si tocca. */
    for (int caso = 0; caso < 8; caso++) {
        PhCfgWidget x[3];
        memcpy(x, w, sizeof x);
        size_t n = 3;
        switch (caso) {
        case 0: x[1].alto = 128; break;
        case 1: x[2].destra = 24; break;
        case 2: x[0].stile = 1; break;
        case 3: x[1].dimensione = PH_CFG_WIDGET_GRANDE; break;
        case 4: x[2].larghezza = 250; x[2].altezza = 128; break;
        case 5: x[0].tipo = PH_CFG_WIDGET_METEO; break;
        case 6: n = 2; break;
        default: x[0].opzione = 3; break;
        }
        int a2[3] = {-7, -7, -7};
        VERIFICA(!ph_dsk_predefiniti_alti(x, n, a2) && a2[0] == -7 && a2[1] == -7 && a2[2] == -7);
    }
    VERIFICA(!ph_dsk_predefiniti_alti(NULL, 3, alti) && !ph_dsk_predefiniti_alti(w, 3, NULL));
}


int main(void)
{
    ph_lingua_imposta(PH_LINGUA_IT);
    prova_griglia();
    prova_cella_libera();
    prova_disponi();
    prova_widget_posto();
    prova_aggancio();
    prova_menu();
    prova_recenti();
    prova_recenti_scansione();
    prova_sfondo();
    prova_misura_immagini();
    prova_viste();
    prova_calendario();
    prova_catalogo();
    prova_orologio_stili();
    prova_forme();
    prova_componenti_anello();
    prova_componenti_fette();
    prova_componenti_curva();
    prova_componenti_tratto();
    prova_componenti_testi();
    prova_componenti_enormi();
    prova_predefiniti_distanziati();
    prova_calendario_mese();
    prova_wx_altri();
    prova_meteo();
    prova_tavolozza_e_cartelle();
    prova_cache();

    ph_risorse_desktop_register_resource();
    if (ph_shell_stile_controlla("/it/yasde/shell/desktop.css") != 0) {
        fprintf(stderr, "desktop.css: errori di sintassi\n");
        errori++;
    }
    /* Vetro (fase 5): nel CSS del pezzo nessun colore scritto a mano fuori dalle eccezioni marcate
     * «colore-dato» (icone sopra lo sfondo, tinte, ombre). Rilevatore comune (shell/prova_css.h). */
    if (ph_prova_css_risorsa("/it/yasde/shell/desktop.css", "desktop.css", true) != 0)
        errori++;
    if (ph_prova_css_autoprova() != 0)
        errori++;
    if (errori == 0)
        printf("prova_desktop: tutto bene\n");
    return errori == 0 ? 0 : 1;
}
