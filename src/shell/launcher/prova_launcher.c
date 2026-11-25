/* Prove di logica di shell-launcher (F-04), SENZA schermo: ricerca (maiuscole,
 * accenti, vuoto), misure del riquadro, colonne, posizione del menu, prima app per Invio, e il CSS
 * del pezzo. Il resto (widget, trascinamento, menu) e' da guardare a occhio:
 * vedi il rapporto. */
#include <stdio.h>
#include <string.h>

#include "logica.h"
#include "shell/prova_css.h"
#include "shell/shell.h"

void ph_risorse_launcher_register_resource(void);

static int errori;

#define CONTROLLA(cond)                                                                            \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            fprintf(stderr, "%s:%d: fallito: %s\n", __FILE__, __LINE__, #cond);                    \
            errori++;                                                                              \
        }                                                                                          \
    } while (0)

/* Il nome `nome` e' trovato dalla ricerca `q` (entrambi grezzi, come arrivano)? */
static bool trovato(const char *nome, const char *q)
{
    char *n = ph_launcher_normalizza(nome, false);
    char *r = ph_launcher_normalizza(q, true);
    bool ok = ph_launcher_corrisponde(n, r);
    g_free(n);
    g_free(r);
    return ok;
}

static void prova_ricerca(void)
{
    CONTROLLA(trovato("Musica", "mu"));          /* criterio di accettazione 2 */
    CONTROLLA(trovato("Musica", "MUS"));         /* maiuscole */
    CONTROLLA(trovato("musica", "MuSiCa"));
    CONTROLLA(!trovato("Musica", "xyz"));
    CONTROLLA(trovato("Musica", ""));            /* ricerca vuota: tutto */
    CONTROLLA(trovato("Musica", "   "));         /* solo spazi = vuota (trim) */
    CONTROLLA(trovato("Musica", "  mu "));
    CONTROLLA(trovato("Orologio", "log"));       /* nel mezzo, come il mockup (indexOf) */
    /* Accenti, in entrambi i sensi. */
    CONTROLLA(trovato("Caff\xc3\xa8", "caffe"));
    CONTROLLA(trovato("Caffe", "caff\xc3\xa8"));
    CONTROLLA(trovato("\xc3\x88 il momento", "e il"));
    CONTROLLA(trovato("Fotocamera", "FOTOCAM"));
    /* Forma scomposta (e + accento combinante) uguale a quella composta. */
    CONTROLLA(trovato("Caffe\xcc\x80", "caff\xc3\xa8"));
    /* Maiuscole non ASCII e sharp s (casefold). */
    CONTROLLA(trovato("\xc3\x9c" "bersicht", "\xc3\xbc" "bersicht"));
    CONTROLLA(trovato("Stra\xc3\x9f" "e", "strasse"));
    /* Testo non valido o NULL: vuoto, senza rompere nulla. */
    char *v = ph_launcher_normalizza("\xff\xfe", false);
    CONTROLLA(v != NULL && v[0] == '\0');
    g_free(v);
    v = ph_launcher_normalizza(NULL, true);
    CONTROLLA(v != NULL && v[0] == '\0');
    g_free(v);
    CONTROLLA(ph_launcher_corrisponde(NULL, ""));
    CONTROLLA(!ph_launcher_corrisponde(NULL, "a"));
    CONTROLLA(ph_launcher_corrisponde("abc", NULL));
}

/* Nomi ostili: arrivano dal telefono (i servizi li puliscono, ma qui non ci si
 * fida): enormi, con caratteri di controllo, ricerca piu' lunga del nome. */
static void prova_ostili(void)
{
    /* Decine di KB: nessun crash, la ricerca nel mezzo e in fondo trova. */
    size_t N = 60 * 1024;
    char *enorme = g_malloc(N + 8);
    memset(enorme, 'a', N);
    memcpy(enorme + N, "zzz", 4);
    CONTROLLA(trovato(enorme, "aaazzz"));
    CONTROLLA(trovato(enorme, "AAAZZZ"));
    CONTROLLA(!trovato(enorme, "zzzz"));
    char *norm = ph_launcher_normalizza(enorme, false);
    CONTROLLA(strlen(norm) == N + 3);
    g_free(norm);
    g_free(enorme);
    /* Ricerca enorme contro un nome corto: nessuna lettura fuori misura. */
    char *lunga = g_malloc(N + 1);
    memset(lunga, 'm', N);
    lunga[N] = '\0';
    CONTROLLA(!trovato("Musica", lunga));
    g_free(lunga);
    /* Ricerca piu' lunga del nome (di poco). */
    CONTROLLA(!trovato("Mu", "Musica"));
    /* Caratteri di controllo e NUL non terminale non possono arrivare in una
     * stringa C; i controlli (U+0001, ESC, U+007F) si trattano come caratteri. */
    CONTROLLA(trovato("Mu\x01sica", "mu\x01s"));
    CONTROLLA(trovato("\x1b[31mRosso", "rosso"));
    CONTROLLA(!trovato("Musica", "\x7f"));
    /* U+0130 (I maiuscola con punto): dopo la scomposizione e il casefold e' "i". */
    CONTROLLA(trovato("\xc4\xb0stanbul", "istanbul"));
    CONTROLLA(trovato("Istanbul", "\xc4\xb0stanbul"));
    CONTROLLA(trovato("\xc4\xb0stanbul", "\xc4\xb0ST"));
    /* Segni combinanti da soli, sequenze troncate, caratteri fuori dal piano base. */
    CONTROLLA(trovato("\xcc\x80\xcc\x81" "ab", "ab"));
    CONTROLLA(!trovato("\xf0\x9f\x98", "a")); /* UTF-8 troncato: nome vuoto */
    CONTROLLA(trovato("\xf0\x9f\x8e\xb5 Musica", "musica"));
}

static void prova_prima(void)
{
    char *n[4];
    const char *grezzi[] = {"Calcolatrice", "Musica", "Impostazioni", "Musicale"};
    for (int i = 0; i < 4; i++)
        n[i] = ph_launcher_normalizza(grezzi[i], false);
    const char *const *nn = (const char *const *)n;
    char *q = ph_launcher_normalizza("MU", true);
    CONTROLLA(ph_launcher_prima(nn, 4, q) == 1); /* la prima nell'ordine del telefono */
    g_free(q);
    q = ph_launcher_normalizza("impo", true);
    CONTROLLA(ph_launcher_prima(nn, 4, q) == 2);
    g_free(q);
    q = ph_launcher_normalizza("xyz", true);
    CONTROLLA(ph_launcher_prima(nn, 4, q) == -1);
    g_free(q);
    q = ph_launcher_normalizza("", true);
    CONTROLLA(ph_launcher_prima(nn, 4, q) == 0); /* ricerca vuota: la prima in assoluto */
    g_free(q);
    CONTROLLA(ph_launcher_prima(nn, 0, "") == -1); /* elenco vuoto */
    CONTROLLA(ph_launcher_prima(NULL, 0, "") == -1);
    for (int i = 0; i < 4; i++)
        g_free(n[i]);
}

static void prova_colonne(void)
{
    /* Colonne da 112 px sulla larghezza interna del riquadro. */
    CONTROLLA(ph_launcher_colonne(112) == 1);
    CONTROLLA(ph_launcher_colonne(223) == 1);
    CONTROLLA(ph_launcher_colonne(224) == 2);
    CONTROLLA(ph_launcher_colonne(432) == 3);   /* riquadro di 480 */
    CONTROLLA(ph_launcher_colonne(912) == 8);   /* riquadro di 960 */
    CONTROLLA(ph_launcher_colonne(111) == 1);   /* minimo 1 */
    CONTROLLA(ph_launcher_colonne(0) == 1);     /* radice non ancora misurata */
    CONTROLLA(ph_launcher_colonne(-50) == 1);
}

static PhLauncherMisure misure(int l, int a)
{
    PhLauncherMisure m;
    memset(&m, 0xff, sizeof m); /* riempito di rifiuto: la funzione deve scrivere tutto */
    ph_launcher_misure(l, a, &m);
    return m;
}

static void prova_misure(void)
{
    PhLauncherMisure m;
    /* 60 % della larghezza, dentro 480..960. */
    m = misure(1000, 800);
    CONTROLLA(m.larghezza == 600);
    m = misure(1280, 800);
    CONTROLLA(m.larghezza == 768);
    m = misure(1920, 1080);
    CONTROLLA(m.larghezza == 960);              /* tetto */
    m = misure(1600, 900);
    CONTROLLA(m.larghezza == 960);              /* 60 % = 960 esatto */
    m = misure(1601, 900);
    CONTROLLA(m.larghezza == 960);              /* 960,6: tetto */
    m = misure(800, 600);
    CONTROLLA(m.larghezza == 480);              /* 60 % = 480 esatto */
    m = misure(700, 600);
    CONTROLLA(m.larghezza == 480);              /* pavimento (60 % = 420) */
    m = misure(528, 600);
    CONTROLLA(m.larghezza == 480);              /* 480 + 2x24: ancora il minimo */
    /* Area stretta: a 24 px dai bordi, come prima. */
    m = misure(527, 600);
    CONTROLLA(m.larghezza == 479);
    m = misure(400, 600);
    CONTROLLA(m.larghezza == 352);
    m = misure(48, 600);
    CONTROLLA(m.larghezza == 0);                /* niente spazio: nessuna misura */
    m = misure(0, 0);
    CONTROLLA(m.larghezza == 0 && m.colonne == 1 && m.altezza_max == 0);
    m = misure(-5, -5);
    CONTROLLA(m.larghezza == 0 && m.margine_alto == 24 && m.altezza_max == 0);
    m = misure(2147483647, 2147483647);          /* nessun overflow */
    CONTROLLA(m.larghezza == 960 && m.altezza_max > 0 && m.margine_alto > 0);

    /* Altezza: massimo 76 % dell'area sopra la barra (48), alto 6 % (minimo 24). */
    m = misure(1280, 800);
    CONTROLLA(m.altezza_max == (800 - 48) * 76 / 100); /* 571 */
    CONTROLLA(m.margine_alto == 48);                    /* 6 % di 800 */
    m = misure(1280, 1000);
    CONTROLLA(m.margine_alto == 60);
    m = misure(1280, 300);
    CONTROLLA(m.margine_alto == 24);                    /* 6 % = 18: minimo 24 */
    CONTROLLA(m.altezza_max == (300 - 48) * 76 / 100);
    m = misure(1280, 48);
    CONTROLLA(m.altezza_max == 0);                      /* solo la barra */
    m = misure(1280, 20);
    CONTROLLA(m.altezza_max == 0);

    /* Larghezza interna, colonne, griglia e campo. */
    m = misure(1280, 800);                              /* riquadro 768 */
    CONTROLLA(m.larghezza_interna == 768 - 48);
    CONTROLLA(m.colonne == 720 / 112 && m.colonne == 6);
    CONTROLLA(m.larghezza_griglia == 6 * 112);
    CONTROLLA(m.larghezza_griglia <= m.larghezza_interna);
    CONTROLLA(m.larghezza_campo == 420);                /* tetto del campo */
    m = misure(800, 600);                               /* riquadro 480 */
    CONTROLLA(m.larghezza_interna == 432 && m.colonne == 3);
    CONTROLLA(m.larghezza_campo == 420);
    m = misure(400, 600);                               /* riquadro 352 */
    CONTROLLA(m.larghezza_interna == 304 && m.colonne == 2);
    CONTROLLA(m.larghezza_campo == 304);                /* il campo non esce dal riquadro */
    m = misure(1920, 1080);                             /* riquadro 960 */
    CONTROLLA(m.colonne == 912 / 112 && m.colonne == 8);
    m = misure(60, 600);                                /* riquadro 12: meno dell'imbottitura */
    CONTROLLA(m.larghezza_interna == 0 && m.colonne == 1 && m.larghezza_campo == 0);
    ph_launcher_misure(800, 600, NULL);                 /* uscita NULL: niente */
}

static void prova_menu(void)
{
    int x = -1, y = -1;
    ph_launcher_posizione_menu(100, 200, 1280, 800, &x, &y); /* dentro: invariato */
    CONTROLLA(x == 100 && y == 200);
    ph_launcher_posizione_menu(1270, 790, 1280, 800, &x, &y); /* spostato dentro */
    CONTROLLA(x == 1280 - 210 && y == 800 - 150);
    ph_launcher_posizione_menu(1070, 650, 1280, 800, &x, &y); /* esattamente al bordo */
    CONTROLLA(x == 1070 && y == 650);
    ph_launcher_posizione_menu(1071, 651, 1280, 800, &x, &y);
    CONTROLLA(x == 1070 && y == 650);
    ph_launcher_posizione_menu(-5, -5, 1280, 800, &x, &y);
    CONTROLLA(x == 0 && y == 0);
    ph_launcher_posizione_menu(50, 50, 100, 100, &x, &y); /* radice piccola: mai negativo */
    CONTROLLA(x == 0 && y == 0);
    ph_launcher_posizione_menu(5, 5, 100, 100, NULL, NULL); /* uscite facoltative */
}

/* Nessun colore scritto a mano nel CSS del pezzo: lo stesso widget sta su vetro chiaro o scuro,
 * un colore fisso puo' diventare invisibile. Rilevatore comune (shell/prova_css.h). */
static void prova_css_senza_colori(void)
{
    CONTROLLA(ph_prova_css_risorsa("/it/yasde/shell/launcher.css", "launcher.css", true) == 0);
    CONTROLLA(ph_prova_css_autoprova() == 0);
}

int main(void)
{
    prova_ricerca();
    prova_ostili();
    prova_prima();
    prova_colonne();
    prova_misure();
    prova_menu();
    ph_risorse_launcher_register_resource();
    if (ph_shell_stile_controlla("/it/yasde/shell/launcher.css") != 0) {
        fprintf(stderr, "launcher.css: errori di sintassi\n");
        errori++;
    }
    prova_css_senza_colori();
    if (errori == 0)
        printf("prova_launcher: tutto bene\n");
    return errori == 0 ? 0 : 1;
}
