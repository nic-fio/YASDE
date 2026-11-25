/* Prove di logica di shell-trasferimento (F-09), SENZA schermo: la logica pura
 * di percorsi.c (normalizzazione, "Su" fino a "/", nome libero, dimensioni in
 * italiano e inglese, testo di una voce, completati tra due fotografie,
 * nascosti, pagine di 200, testi ostili) e il CSS del pezzo. I widget (elenchi,
 * pannelli, trascinamento) si provano a occhio con il demo: GTK qui non ha
 * schermo. */
#include <locale.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config/lingua.h"
#include "shell/prova_css.h"
#include "shell/shell.h"
#include "shell/trasferimento/percorsi.h"

void ph_risorse_trasferimento_register_resource(void);

static int errori;
#define CONTROLLA(c)                                                                               \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            fprintf(stderr, "%s:%d: fallito: %s\n", __FILE__, __LINE__, #c);                       \
            errori++;                                                                              \
        }                                                                                          \
    } while (0)

static void normalizzazione(void)
{
    char o[PH_TRF_PERCORSO_MAX + 1];
    struct {
        const char *in, *atteso;
    } si[] = {
        {"", "/"},
        {"/", "/"},
        {"   ", "/"},
        {"/sdcard", "/storage/emulated/0"},
        {"/sdcard/", "/storage/emulated/0"},
        {"/SDCARD/Download/", "/storage/emulated/0/Download"},
        {"  /sdcard/Download  ", "/storage/emulated/0/Download"},
        {"/sdcard/Download///", "/storage/emulated/0/Download"},
        {"/sdcardx", "/sdcardx"}, /* non e' la scorciatoia */
        {"/sdcard2/foto", "/sdcard2/foto"},
        {"sdcard/x", "/storage/emulated/0/x"},
        {"storage/emulated", "/storage/emulated"},
        {"//a//b/./c/../d", "/a/b/d"},
        {"/../../x", "/x"}, /* mai sopra "/" */
        {"/a/..", "/"},
        {"/data", "/data"},
        {"/nonc'\xc3\xa8", "/nonc'\xc3\xa8"},
        {"/cartella con spazi/e\xc3\xa0", "/cartella con spazi/e\xc3\xa0"},
        {"/pippo\n", "/pippo"}, /* l'a capo ai lati e' uno spazio */
        {"//sdcard/x", "/storage/emulated/0/x"},
        {"/./sdcard/x", "/storage/emulated/0/x"},
        {"/sdcard/..", "/"},
    };
    for (size_t i = 0; i < sizeof si / sizeof si[0]; i++) {
        bool ok = ph_trf_normalizza(si[i].in, o, sizeof o);
        if (!ok || strcmp(o, si[i].atteso) != 0) {
            fprintf(stderr, "normalizza(\"%s\") = %d \"%s\", atteso \"%s\"\n", si[i].in, ok, o, si[i].atteso);
            errori++;
        }
    }
    /* Ostili: a capo o controlli in mezzo, UTF-8 non valido, troppo lungo, NULL, buffer piccolo. */
    CONTROLLA(!ph_trf_normalizza("/a\nb", o, sizeof o));
    CONTROLLA(strcmp(o, "/") == 0); /* mai un percorso a meta' */
    CONTROLLA(!ph_trf_normalizza("/a\tb", o, sizeof o));
    CONTROLLA(!ph_trf_normalizza("/a\x01", o, sizeof o));
    CONTROLLA(!ph_trf_normalizza("/a\x7f", o, sizeof o));
    CONTROLLA(!ph_trf_normalizza("/a\xc2\x85", o, sizeof o)); /* controllo C1 */
    CONTROLLA(!ph_trf_normalizza("/a\xff\xfe", o, sizeof o));
    CONTROLLA(!ph_trf_normalizza(NULL, o, sizeof o));
    char *lungo = g_malloc(PH_TRF_PERCORSO_MAX + 100);
    memset(lungo, 'a', PH_TRF_PERCORSO_MAX + 99);
    lungo[0] = '/';
    lungo[PH_TRF_PERCORSO_MAX + 99] = '\0';
    CONTROLLA(!ph_trf_normalizza(lungo, o, sizeof o));
    /* Al limite: PH_TRF_PERCORSO_MAX byte entrano. */
    lungo[PH_TRF_PERCORSO_MAX] = '\0';
    CONTROLLA(ph_trf_normalizza(lungo, o, sizeof o) && strlen(o) == PH_TRF_PERCORSO_MAX);
    g_free(lungo);
    char piccolo[4];
    CONTROLLA(!ph_trf_normalizza("/abcdef", piccolo, sizeof piccolo));
    CONTROLLA(!ph_trf_normalizza("/abc", piccolo, 0));
}

static void normalizzazione_pc(void)
{
    char o[PH_TRF_PERCORSO_MAX + 1];
    CONTROLLA(ph_trf_normalizza_pc("/home/x/", NULL, o, sizeof o) && strcmp(o, "/home/x") == 0);
    CONTROLLA(ph_trf_normalizza_pc("/sdcard", NULL, o, sizeof o) && strcmp(o, "/sdcard") == 0); /* niente scorciatoia */
    CONTROLLA(ph_trf_normalizza_pc("y/z", "/home/x", o, sizeof o) && strcmp(o, "/home/x/y/z") == 0);
    CONTROLLA(ph_trf_normalizza_pc("../w", "/home/x", o, sizeof o) && strcmp(o, "/home/w") == 0);
    CONTROLLA(ph_trf_normalizza_pc("", "/home/x", o, sizeof o) && strcmp(o, "/") == 0);
    CONTROLLA(ph_trf_normalizza_pc("~", NULL, o, sizeof o) && strcmp(o, g_get_home_dir()) == 0);
    CONTROLLA(ph_trf_normalizza_pc("/home/.nascosta", NULL, o, sizeof o) && strcmp(o, "/home/.nascosta") == 0);
    CONTROLLA(!ph_trf_normalizza_pc("/a\nb", NULL, o, sizeof o));
    CONTROLLA(!ph_trf_normalizza_pc("/a\xff", NULL, o, sizeof o));
}

static PhSrvTrasferimento voce(uint64_t id, PhSrvVerso verso, PhSrvTrStato stato);

static void scelta_voci(void)
{
    /* 40 voci: la 1 in corso, 2..30 in attesa, 31..40 finite; piu' una dell'altro verso. */
    PhSrvTrasferimento v[41];
    for (int i = 0; i < 40; i++)
        v[i] = voce((uint64_t)i + 1, PH_SRV_INVIO, i == 0 ? PH_SRV_TR_CORSO : i < 30 ? PH_SRV_TR_ATTESA : PH_SRV_TR_COMPLETATO);
    v[40] = voce(41, PH_SRV_RICEZIONE, PH_SRV_TR_CORSO);
    size_t out[12], nasc = 99;
    size_t k = ph_trf_scegli_voci(v, 41, PH_SRV_INVIO, 12, out, &nasc);
    /* 30 da fare: ne stanno 12 (la in corso per prima), 18 restano fuori, nessuna finita. */
    CONTROLLA(k == 12 && nasc == 18 && out[0] == 0 && out[1] == 1 && out[11] == 11);
    /* Poche da fare: poi le finite, la piu' recente prima. */
    PhSrvTrasferimento w[6];
    w[0] = voce(1, PH_SRV_INVIO, PH_SRV_TR_COMPLETATO);
    w[1] = voce(2, PH_SRV_INVIO, PH_SRV_TR_ATTESA);
    w[2] = voce(3, PH_SRV_INVIO, PH_SRV_TR_FALLITO);
    w[3] = voce(4, PH_SRV_INVIO, PH_SRV_TR_CORSO);
    w[4] = voce(5, PH_SRV_RICEZIONE, PH_SRV_TR_CORSO);
    w[5] = voce(6, PH_SRV_INVIO, PH_SRV_TR_ANNULLATO);
    k = ph_trf_scegli_voci(w, 6, PH_SRV_INVIO, 12, out, &nasc);
    CONTROLLA(k == 5 && nasc == 0 && out[0] == 3 && out[1] == 1 && out[2] == 5 && out[3] == 2 && out[4] == 0);
    k = ph_trf_scegli_voci(w, 6, PH_SRV_INVIO, 3, out, &nasc);
    CONTROLLA(k == 3 && nasc == 0 && out[2] == 5);
    k = ph_trf_scegli_voci(NULL, 0, PH_SRV_INVIO, 12, out, NULL);
    CONTROLLA(k == 0);
}

static void su_e_unisci(void)
{
    char o[PH_TRF_PERCORSO_MAX + 1];
    /* Si risale fino a "/" e li' ci si ferma. */
    char p[PH_TRF_PERCORSO_MAX + 1];
    g_strlcpy(p, "/storage/emulated/0", sizeof p);
    const char *attesi[] = {"/storage/emulated", "/storage", "/"};
    for (size_t i = 0; i < 3; i++) {
        CONTROLLA(ph_trf_su(p, o, sizeof o));
        CONTROLLA(strcmp(o, attesi[i]) == 0);
        g_strlcpy(p, o, sizeof p);
    }
    CONTROLLA(!ph_trf_su("/", o, sizeof o));
    CONTROLLA(strcmp(o, "/") == 0);
    CONTROLLA(ph_trf_e_radice("/") && !ph_trf_e_radice("/a") && !ph_trf_e_radice(NULL));
    CONTROLLA(ph_trf_su("/a/", o, sizeof o) && strcmp(o, "/") == 0);
    CONTROLLA(!ph_trf_su("relativo", o, sizeof o));
    /* Anche per il PC: "/home/x" -> "/home" -> "/". */
    CONTROLLA(ph_trf_su("/home/x", o, sizeof o) && strcmp(o, "/home") == 0);
    CONTROLLA(ph_trf_su("/home", o, sizeof o) && strcmp(o, "/") == 0);
    char piccolo[3];
    CONTROLLA(!ph_trf_su("/aaaa/bbbb", piccolo, sizeof piccolo));

    CONTROLLA(ph_trf_unisci("/", "a", o, sizeof o) && strcmp(o, "/a") == 0);
    CONTROLLA(ph_trf_unisci("/a", "b", o, sizeof o) && strcmp(o, "/a/b") == 0);
    CONTROLLA(ph_trf_unisci("/a/", "b c", o, sizeof o) && strcmp(o, "/a/b c") == 0);
    CONTROLLA(ph_trf_unisci("/a", "riga\nnuova", o, sizeof o) && strcmp(o, "/a/riga\nnuova") == 0);
    CONTROLLA(!ph_trf_unisci("/a", "b/c", o, sizeof o));
    CONTROLLA(!ph_trf_unisci("/a", "..", o, sizeof o));
    CONTROLLA(!ph_trf_unisci("/a", ".", o, sizeof o));
    CONTROLLA(!ph_trf_unisci("/a", "", o, sizeof o));
    CONTROLLA(!ph_trf_unisci("/a", NULL, o, sizeof o));
    CONTROLLA(!ph_trf_unisci("/aaaa", "bbbb", piccolo, sizeof piccolo));
    CONTROLLA(ph_trf_nome_valido(".nascosto") && ph_trf_nome_valido("a b"));
    CONTROLLA(!ph_trf_nome_valido("") && !ph_trf_nome_valido("..") && !ph_trf_nome_valido("a/b"));
    /* Un nome di 255 byte dentro un percorso lungo non fa uscire dal buffer. */
    char *nome = g_malloc(256);
    memset(nome, 'n', 255);
    nome[255] = '\0';
    char *cartella = g_malloc(PH_TRF_PERCORSO_MAX);
    memset(cartella, 'c', PH_TRF_PERCORSO_MAX - 1);
    cartella[0] = '/';
    cartella[PH_TRF_PERCORSO_MAX - 1] = '\0';
    CONTROLLA(!ph_trf_unisci(cartella, nome, o, sizeof o));
    g_free(nome);
    g_free(cartella);
}

static void nomi_liberi(void)
{
    char o[128];
    const char *nessuno[] = {"altro"};
    CONTROLLA(ph_trf_nome_libero("Nuova cartella", nessuno, 1, o, sizeof o) && strcmp(o, "Nuova cartella") == 0);
    const char *uno[] = {"Nuova cartella"};
    CONTROLLA(ph_trf_nome_libero("Nuova cartella", uno, 1, o, sizeof o) && strcmp(o, "Nuova cartella 2") == 0);
    /* La seconda volta: "Nuova cartella" e "Nuova cartella 2" esistono, si crea "3". */
    const char *due[] = {"Nuova cartella", "Nuova cartella 2"};
    CONTROLLA(ph_trf_nome_libero("Nuova cartella", due, 2, o, sizeof o) && strcmp(o, "Nuova cartella 3") == 0);
    /* Maiuscole e minuscole non contano; i buchi si riempiono. */
    const char *buco[] = {"NUOVA CARTELLA", "nuova cartella 3"};
    CONTROLLA(ph_trf_nome_libero("Nuova cartella", buco, 2, o, sizeof o) && strcmp(o, "Nuova cartella 2") == 0);
    CONTROLLA(ph_trf_nome_libero("Nuova cartella", NULL, 0, o, sizeof o) && strcmp(o, "Nuova cartella") == 0);
    char piccolo[8];
    CONTROLLA(!ph_trf_nome_libero("Nuova cartella", NULL, 0, piccolo, sizeof piccolo));
    CONTROLLA(!ph_trf_nome_libero("", NULL, 0, o, sizeof o));
    /* Con molti nomi presi ne trova uno libero. */
    const char *molti[50];
    char *nomi[50];
    for (int i = 0; i < 50; i++) {
        nomi[i] = i == 0 ? g_strdup("N") : g_strdup_printf("N %d", i + 1);
        molti[i] = nomi[i];
    }
    CONTROLLA(ph_trf_nome_libero("N", molti, 50, o, sizeof o) && strcmp(o, "N 51") == 0);
    for (int i = 0; i < 50; i++)
        g_free(nomi[i]);
}

static void nascosti_e_pagine(void)
{
    CONTROLLA(ph_trf_nascosto(".nascosto") && !ph_trf_nascosto("visibile") && !ph_trf_nascosto("") &&
              !ph_trf_nascosto(NULL) && !ph_trf_nascosto("a.b"));
    CONTROLLA(ph_trf_percorso_nascosto("/a/.b/c") && ph_trf_percorso_nascosto("/.cache") &&
              !ph_trf_percorso_nascosto("/a/b") && !ph_trf_percorso_nascosto("/a/b.c") &&
              !ph_trf_percorso_nascosto(NULL));

    CONTROLLA(PH_TRF_PAGINA == 200);
    CONTROLLA(ph_trf_pagina_visibili(0, 1) == 0);
    CONTROLLA(ph_trf_pagina_visibili(150, 1) == 150);
    CONTROLLA(ph_trf_pagina_visibili(200, 1) == 200);
    CONTROLLA(ph_trf_pagina_visibili(450, 1) == 200);
    CONTROLLA(ph_trf_pagina_visibili(450, 2) == 400);
    CONTROLLA(ph_trf_pagina_visibili(450, 3) == 450);
    CONTROLLA(ph_trf_pagina_visibili(450, 99) == 450);
    CONTROLLA(ph_trf_pagina_visibili(10, SIZE_MAX) == 10);
    CONTROLLA(ph_trf_pagina_visibili(450, 0) == 0);
    CONTROLLA(ph_trf_pagina_prossime(450, 200) == 200);
    CONTROLLA(ph_trf_pagina_prossime(450, 400) == 50);
    CONTROLLA(ph_trf_pagina_prossime(450, 450) == 0);
    CONTROLLA(ph_trf_pagina_prossime(150, 150) == 0);
    CONTROLLA(ph_trf_pagina_prossime(201, 200) == 1);
    CONTROLLA(ph_trf_pagina_prossime(10, 99) == 0);
}

static void testi(void)
{
    char *s = ph_trf_testo_sicuro("a\nb\r\tc\x1b[31m", 255);
    CONTROLLA(strcmp(s, "a?b??c?[31m") == 0);
    g_free(s);
    s = ph_trf_testo_sicuro("\xe2\x80\xae" "gpj.exe", 255); /* inversione di direzione */
    CONTROLLA(strcmp(s, "?gpj.exe") == 0);
    g_free(s);
    s = ph_trf_testo_sicuro("ok\xff\xfe" "fine", 255); /* UTF-8 non valido */
    CONTROLLA(g_utf8_validate(s, -1, NULL) && strstr(s, "ok") == s && strstr(s, "fine") != NULL);
    g_free(s);
    s = ph_trf_testo_sicuro(NULL, 255);
    CONTROLLA(strcmp(s, "") == 0);
    g_free(s);
    char *lungo = g_malloc(1001);
    memset(lungo, 'x', 1000);
    lungo[1000] = '\0';
    s = ph_trf_testo_sicuro(lungo, 255);
    CONTROLLA(strlen(s) <= 255 && g_str_has_suffix(s, "\xe2\x80\xa6") && g_utf8_validate(s, -1, NULL));
    g_free(s);
    /* Il taglio non spezza un carattere a due byte. */
    memset(lungo, 0, 1001);
    for (int i = 0; i < 400; i++)
        memcpy(lungo + i * 2, "\xc3\xa8", 2);
    s = ph_trf_testo_sicuro(lungo, 255);
    CONTROLLA(strlen(s) <= 255 && g_utf8_validate(s, -1, NULL));
    g_free(s);
    g_free(lungo);
    s = ph_trf_testo_sicuro("corto", 255);
    CONTROLLA(strcmp(s, "corto") == 0);
    g_free(s);
}

/* Il separatore decimale viene dalla lingua dell'APP, non da quella del PC:
 * con una localizzazione a virgola l'app inglese scrive ancora «40.0 MB». */
static void dimensioni_locale_pc(void)
{
    const char *nomi[] = {"it_IT.UTF-8", "it_IT.utf8", "de_DE.UTF-8", "fr_FR.UTF-8"};
    bool fatto = false;
    char o[64];
    for (size_t i = 0; i < sizeof nomi / sizeof nomi[0] && !fatto; i++) {
        if (setlocale(LC_ALL, nomi[i]) == NULL || localeconv()->decimal_point[0] != ',')
            continue;
        fatto = true;
        ph_lingua_imposta(PH_LINGUA_EN);
        ph_trf_dimensione(UINT64_C(40) << 20, o, sizeof o);
        CONTROLLA(strcmp(o, "40.0 MB") == 0);
        ph_trf_dimensione(UINT64_C(3) << 29, o, sizeof o);
        CONTROLLA(strcmp(o, "1.5 GB") == 0);
        ph_lingua_imposta(PH_LINGUA_IT);
        ph_trf_dimensione(UINT64_C(40) << 20, o, sizeof o);
        CONTROLLA(strcmp(o, "40,0 MB") == 0);
    }
    setlocale(LC_ALL, "C");
    if (!fatto)
        fprintf(stderr, "SALTATA la prova delle dimensioni con lingua del PC a virgola: nessuna localizzazione disponibile nel container\n");
}

static void dimensioni_e_stati(void)
{
    char o[128];
    ph_lingua_imposta(PH_LINGUA_IT);
    struct {
        uint64_t b;
        const char *it, *en;
    } d[] = {
        {0, "0 B", "0 B"},
        {1023, "1023 B", "1023 B"},
        {1024, "1 KB", "1 KB"},
        {1536, "2 KB", "2 KB"},
        {4096, "4 KB", "4 KB"},
        {1048575, "1024 KB", "1024 KB"}, /* come Math.round del mockup */
        {1048576, "1,0 MB", "1.0 MB"},
        {2516582, "2,4 MB", "2.4 MB"},
        {90177536, "86,0 MB", "86.0 MB"},
        {UINT64_C(1) << 30, "1,0 GB", "1.0 GB"},
        {UINT64_C(5) << 30, "5,0 GB", "5.0 GB"},
    };
    for (size_t i = 0; i < sizeof d / sizeof d[0]; i++) {
        ph_lingua_imposta(PH_LINGUA_IT);
        ph_trf_dimensione(d[i].b, o, sizeof o);
        if (strcmp(o, d[i].it) != 0) {
            fprintf(stderr, "dimensione(%llu) it = \"%s\", atteso \"%s\"\n", (unsigned long long)d[i].b, o, d[i].it);
            errori++;
        }
        ph_lingua_imposta(PH_LINGUA_EN);
        ph_trf_dimensione(d[i].b, o, sizeof o);
        if (strcmp(o, d[i].en) != 0) {
            fprintf(stderr, "dimensione(%llu) en = \"%s\", atteso \"%s\"\n", (unsigned long long)d[i].b, o, d[i].en);
            errori++;
        }
    }
    char piccolo[4];
    ph_lingua_imposta(PH_LINGUA_IT);
    ph_trf_dimensione(UINT64_MAX, piccolo, sizeof piccolo); /* non esce dal buffer */
    CONTROLLA(strlen(piccolo) < sizeof piccolo);

    CONTROLLA(ph_trf_percento(0, 0) == 0);
    CONTROLLA(ph_trf_percento(50, 0) == 0);
    CONTROLLA(ph_trf_percento(50, 100) == 50);
    CONTROLLA(ph_trf_percento(1, 3) == 33);
    CONTROLLA(ph_trf_percento(100, 100) == 100);
    CONTROLLA(ph_trf_percento(900, 100) == 100);
    CONTROLLA(ph_trf_percento(UINT64_MAX - 1, UINT64_MAX) == 99);

    /* Testo dello stato (italiano e inglese). */
    ph_trf_stato_testo(PH_SRV_TR_CORSO, 42, o, sizeof o);
    CONTROLLA(strcmp(o, "42%") == 0);
    ph_trf_stato_testo(PH_SRV_TR_CORSO, 500, o, sizeof o);
    CONTROLLA(strcmp(o, "100%") == 0);
    ph_trf_stato_testo(PH_SRV_TR_COMPLETATO, 100, o, sizeof o);
    CONTROLLA(strcmp(o, "Completato") == 0);
    ph_trf_stato_testo(PH_SRV_TR_ATTESA, 0, o, sizeof o);
    CONTROLLA(strcmp(o, "In attesa") == 0);
    ph_trf_stato_testo(PH_SRV_TR_ANNULLATO, 0, o, sizeof o);
    CONTROLLA(strcmp(o, "Annullato") == 0);
    ph_trf_stato_testo(PH_SRV_TR_FALLITO, 0, o, sizeof o);
    CONTROLLA(strcmp(o, "Non riuscito") == 0);
    ph_lingua_imposta(PH_LINGUA_EN);
    ph_trf_stato_testo(PH_SRV_TR_COMPLETATO, 100, o, sizeof o);
    CONTROLLA(strcmp(o, "Completed") == 0);
    ph_trf_stato_testo(PH_SRV_TR_ATTESA, 0, o, sizeof o);
    CONTROLLA(strcmp(o, "Waiting") == 0);
    ph_lingua_imposta(PH_LINGUA_IT);

    /* Riga sotto la barra. */
    char nome[] = "relazione.pdf", orig[] = "/tmp/relazione.pdf", dest[] = "/storage/emulated/0/Download/relazione.pdf";
    char err[] = "Spazio esaurito sul telefono";
    PhSrvTrasferimento t = {.id = 1, .verso = PH_SRV_INVIO, .stato = PH_SRV_TR_CORSO, .nome = nome,
                            .origine = orig, .destinazione = dest, .errore = (char *)"", .dimensione = 2516582,
                            .fatti = 1000};
    char meta[512];
    ph_trf_meta_testo(&t, meta, sizeof meta);
    CONTROLLA(strcmp(meta, "2,4 MB \xc2\xb7 /storage/emulated/0/Download/relazione.pdf") == 0);
    t.stato = PH_SRV_TR_FALLITO;
    t.errore = err;
    ph_trf_meta_testo(&t, meta, sizeof meta);
    CONTROLLA(strcmp(meta, "2,4 MB \xc2\xb7 Spazio esaurito sul telefono") == 0);
    t.dimensione = 0; /* ancora ignota: solo la destinazione */
    t.stato = PH_SRV_TR_ATTESA;
    ph_trf_meta_testo(&t, meta, sizeof meta);
    CONTROLLA(strcmp(meta, dest) == 0);
    /* Destinazione ostile: a capo e controlli non arrivano all'interfaccia; buffer piccolo. */
    char ostile[] = "/storage/a\nb\x1b" "c";
    t.destinazione = ostile;
    t.dimensione = 10;
    ph_trf_meta_testo(&t, meta, sizeof meta);
    CONTROLLA(strchr(meta, '\n') == NULL && strchr(meta, '\x1b') == NULL && strstr(meta, "a?b?c") != NULL);
    char poco[8];
    ph_trf_meta_testo(&t, poco, sizeof poco);
    CONTROLLA(strlen(poco) < sizeof poco);
    CONTROLLA(ph_trf_attivo(PH_SRV_TR_ATTESA) && ph_trf_attivo(PH_SRV_TR_CORSO) &&
              !ph_trf_attivo(PH_SRV_TR_COMPLETATO) && !ph_trf_attivo(PH_SRV_TR_FALLITO) &&
              !ph_trf_attivo(PH_SRV_TR_ANNULLATO));
}

static PhSrvTrasferimento voce(uint64_t id, PhSrvVerso verso, PhSrvTrStato stato)
{
    PhSrvTrasferimento t = {0};
    t.id = id;
    t.verso = verso;
    t.stato = stato;
    return t;
}

static void completati(void)
{
    PhTrfFoto *f = ph_trf_foto_nuova();
    PhSrvTrasferimento a[] = {voce(1, PH_SRV_INVIO, PH_SRV_TR_CORSO), voce(2, PH_SRV_RICEZIONE, PH_SRV_TR_ATTESA),
                              voce(3, PH_SRV_INVIO, PH_SRV_TR_COMPLETATO)};
    /* Prima fotografia silenziosa: il 3 era gia' finito. */
    ph_trf_foto_riprendi(f, a, 3);
    GArray *g = ph_trf_foto_completati(f, a, 3);
    CONTROLLA(g->len == 0);
    g_array_free(g, TRUE);

    PhSrvTrasferimento b[] = {voce(1, PH_SRV_INVIO, PH_SRV_TR_COMPLETATO), voce(2, PH_SRV_RICEZIONE, PH_SRV_TR_CORSO),
                              voce(3, PH_SRV_INVIO, PH_SRV_TR_COMPLETATO), voce(4, PH_SRV_RICEZIONE, PH_SRV_TR_COMPLETATO),
                              voce(5, PH_SRV_INVIO, PH_SRV_TR_FALLITO)};
    g = ph_trf_foto_completati(f, b, 5);
    /* 1 e' passato a COMPLETATO, 3 lo era gia', 4 e' nuovo e gia' completato, 5 e' fallito. */
    CONTROLLA(g->len == 2 && g_array_index(g, guint, 0) == 0 && g_array_index(g, guint, 1) == 3);
    g_array_free(g, TRUE);
    g = ph_trf_foto_completati(f, b, 5); /* stessa fotografia: niente di nuovo */
    CONTROLLA(g->len == 0);
    g_array_free(g, TRUE);

    PhSrvTrasferimento c[] = {voce(2, PH_SRV_RICEZIONE, PH_SRV_TR_COMPLETATO)};
    g = ph_trf_foto_completati(f, c, 1); /* ora anche il 2 e' finito; gli altri sono spariti dalla coda */
    CONTROLLA(g->len == 1 && g_array_index(g, guint, 0) == 0);
    g_array_free(g, TRUE);
    g = ph_trf_foto_completati(f, NULL, 0);
    CONTROLLA(g->len == 0);
    g_array_free(g, TRUE);
    ph_trf_foto_libera(f);
    ph_trf_foto_libera(NULL);
}

/* Il CSS del pezzo non ha colori a mano fuori dalle eccezioni marcate. Rilevatore comune
 * (shell/prova_css.h), con la sua autoprova. */
static void css_senza_colori(void)
{
    CONTROLLA(ph_prova_css_risorsa("/it/yasde/shell/trasferimento.css", "trasferimento.css", true) == 0);
    CONTROLLA(ph_prova_css_autoprova() == 0);
}

int main(void)
{
    ph_lingua_imposta(PH_LINGUA_IT);
    ph_risorse_trasferimento_register_resource();
    if (ph_shell_stile_controlla("/it/yasde/shell/trasferimento.css") != 0) {
        fprintf(stderr, "trasferimento.css: errori di sintassi\n");
        errori++;
    }
    normalizzazione();
    normalizzazione_pc();
    scelta_voci();
    su_e_unisci();
    nomi_liberi();
    nascosti_e_pagine();
    testi();
    dimensioni_e_stati();
    dimensioni_locale_pc();
    completati();
    css_senza_colori();
    if (errori == 0)
        printf("prova_trasferimento: tutto bene\n");
    return errori == 0 ? 0 : 1;
}
