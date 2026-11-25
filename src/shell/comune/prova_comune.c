/* Prova del pezzo comune della shell SENZA schermo: consegna al thread
 * principale (ordine, mai dentro la chiamata, da altri thread), registro
 * delle decisioni, sintassi dello stile di base. */
#include <stdio.h>
#include <glib/gstdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "interno.h"
#include "shell/servizi.h"
#include "shell/servizi_backend.h"
#include "shell/shell.h"

static int errori;
#define CONTROLLA(c)                                                                               \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            fprintf(stderr, "%s:%d: fallito: %s\n", __FILE__, __LINE__, #c);                       \
            errori++;                                                                              \
        }                                                                                          \
    } while (0)

#define N 1000
static int arrivati[2];
static int fuori_ordine, fuori_thread;
static int liberati;

static void ricevi(void *dati)
{
    int v = GPOINTER_TO_INT(dati);
    int quale = v / 100000, n = v % 100000;
    if (n != arrivati[quale])
        fuori_ordine++;
    arrivati[quale]++;
    if (!ph_shell_nel_thread_gtk())
        fuori_thread++;
}

static void libera(void *dati)
{
    (void)dati;
    liberati++;
}

static gpointer manda(gpointer p)
{
    (void)p;
    for (int i = 0; i < N; i++)
        ph_shell_consegna(ricevi, GINT_TO_POINTER(100000 + i), libera);
    return NULL;
}

/* ---- Icone del tema Kora (ph_srv_icona) ---------------------------------- */
static GString *richieste; /* pacchetti chiesti al telefono, separati da spazio */
static int avvisi_icona;

static void icona_chiedi_finta(void *b, const char *pacchetto, int lato)
{
    (void)b;
    (void)lato;
    g_string_append_printf(richieste, "%s ", pacchetto);
}

static void avviso_icona(PhServizi *s, PhSrvEvento ev, const char *arg, void *utente)
{
    (void)s;
    (void)ev;
    (void)arg;
    (void)utente;
    avvisi_icona++;
}

/* Gira il ciclo principale finche' *v non arriva ad `atteso` (scadenza 5 s). */
static bool aspetta_int(const int *v, int atteso)
{
    gint64 fine = g_get_monotonic_time() + 5000000;
    while (*v != atteso && g_get_monotonic_time() < fine)
        g_main_context_iteration(NULL, FALSE);
    return *v == atteso;
}

static GBytes *png_colore(int lato, uint8_t r, uint8_t g, uint8_t b)
{
    uint8_t *px = g_malloc((gsize)lato * lato * 4);
    for (int i = 0; i < lato * lato; i++) {
        px[i * 4] = r;
        px[i * 4 + 1] = g;
        px[i * 4 + 2] = b;
        px[i * 4 + 3] = 255;
    }
    GBytes *d = g_bytes_new_take(px, (gsize)lato * lato * 4);
    GdkTexture *t = gdk_memory_texture_new(lato, lato, GDK_MEMORY_R8G8B8A8, d, (gsize)lato * 4);
    g_bytes_unref(d);
    GBytes *png = gdk_texture_save_to_png_bytes(t);
    g_object_unref(t);
    return png;
}

/* Il primo pixel come numero: serve a riconoscere la texture. */
static uint32_t pixel(GdkTexture *t)
{
    GdkTextureDownloader *d = gdk_texture_downloader_new(t);
    gdk_texture_downloader_set_format(d, GDK_MEMORY_R8G8B8A8);
    gsize riga = (gsize)gdk_texture_get_width(t) * 4;
    GBytes *b = gdk_texture_downloader_download_bytes(d, &riga);
    gdk_texture_downloader_free(d);
    uint32_t r = 0;
    if (g_bytes_get_size(b) >= 4)
        memcpy(&r, g_bytes_get_data(b, NULL), 4);
    g_bytes_unref(b);
    return r;
}

#define ROSSO 0xff0000ffu /* R8G8B8A8 little endian: R in basso */
#define VERDE 0xff00ff00u
#define BLU 0xffff0000u

static void scrivi(const char *dir, const char *nome, const void *dati, size_t n)
{
    char *p = g_build_filename(dir, nome, NULL);
    CONTROLLA(g_file_set_contents(p, dati, (gssize)n, NULL));
    g_free(p);
}

static void scrivi_png(const char *dir, const char *nome, int lato, uint8_t r, uint8_t g, uint8_t b)
{
    GBytes *png = png_colore(lato, r, g, b);
    scrivi(dir, nome, g_bytes_get_data(png, NULL), g_bytes_get_size(png));
    g_bytes_unref(png);
}

static void togli_cartella(const char *dir)
{
    GDir *d = g_dir_open(dir, 0, NULL);
    const char *n;
    while (d != NULL && (n = g_dir_read_name(d)) != NULL) {
        char *p = g_build_filename(dir, n, NULL);
        if (g_file_test(p, G_FILE_TEST_IS_DIR) && !g_file_test(p, G_FILE_TEST_IS_SYMLINK))
            togli_cartella(p);
        else
            g_remove(p);
        g_free(p);
    }
    if (d != NULL)
        g_dir_close(d);
    g_rmdir(dir);
}

/* Servizi con un backend finto e un telefono "collegato". */
static PhServizi *servizi_prova(void)
{
    PhServiziBackend vt;
    memset(&vt, 0, sizeof vt);
    vt.icona_chiedi = icona_chiedi_finta;
    PhServizi *s = ph_servizi_nuovi(&vt, NULL);
    CONTROLLA(s != NULL);
    PhSrvCollegamento c;
    memset(&c, 0, sizeof c);
    c.stato = PH_SRV_COLLEGATO;
    c.ha_telefono = true;
    ph_servizi_pubblica_collegamento(s, &c);
    gint64 fine = g_get_monotonic_time() + 5000000;
    PhSrvCollegamento letto = {0};
    while (!letto.ha_telefono && g_get_monotonic_time() < fine) {
        g_main_context_iteration(NULL, FALSE);
        ph_srv_collegamento(s, &letto);
    }
    CONTROLLA(letto.ha_telefono);
    g_string_truncate(richieste, 0);
    return s;
}

static bool chiesto(const char *pk)
{
    char *k = g_strdup_printf("%s ", pk);
    bool si = strstr(richieste->str, k) != NULL && (richieste->str == strstr(richieste->str, k) ||
                                                    strstr(richieste->str, k)[-1] == ' ');
    g_free(k);
    return si;
}

static void mkdir_p(const char *p)
{
    CONTROLLA(g_mkdir_with_parents(p, 0700) == 0);
}

/* Riempie g con commenti fino a esattamente `len` byte, a righe lunghe (1000
 * byte): cosi' restano ben sotto il tetto delle righe. */
static void riempi_commenti(GString *g, size_t len)
{
    while (g->len < len) {
        size_t r = len - g->len;
        size_t riga = r < 1000 ? r : 1000;
        if (riga == 1) {
            g_string_append_c(g, '\n');
            continue;
        }
        g_string_append_c(g, '#');
        for (size_t i = 0; i + 2 < riga; i++)
            g_string_append_c(g, 'x');
        g_string_append_c(g, '\n');
    }
}

static void prova_icone_kora(void)
{
    richieste = g_string_new(NULL);
    char *base = g_dir_make_tmp("yasde-prova-kora-XXXXXX", NULL);
    CONTROLLA(base != NULL);
    if (base == NULL)
        return;
    char *dir = g_build_filename(base, "kora", NULL);
    mkdir_p(dir);
    scrivi_png(dir, "verde.png", 128, 0, 255, 0);
    scrivi_png(dir, "blu.png", 128, 0, 0, 255);
    scrivi_png(dir, "rosso.png", 128, 255, 0, 0);
    scrivi_png(dir, "piccolo.png", 64, 0, 0, 255);
    scrivi(dir, "rotto.png", "questo non e' un png", 20);
    /* Bersagli dei nomi pericolosi: esistono davvero, validi, di colore noto. */
    scrivi_png(base, "fuori.png", 128, 255, 0, 0);
    char *sub = g_build_filename(dir, "a", NULL);
    mkdir_p(sub);
    scrivi_png(sub, "b.png", 128, 255, 0, 0);
    g_free(sub);
    scrivi_png(dir, ".nascosto.png", 128, 255, 0, 0);
    char lunga[600];
    memset(lunga, 'a', sizeof lunga - 1);
    lunga[sizeof lunga - 1] = '\0';
    char *tab = g_strdup_printf("# commento\n\ncom.a verde\nmalformata\ncom.b  rosso\ncom.c ../fuori\n"
                                "com.d a/b\ncom.e .nascosto\n%s verde\ncom.f nonesiste\n"
                                "com.a rosso\ncom.h rotto\ncom.i piccolo\r\ncom.g blu",
                                lunga);
    scrivi(dir, "tabella.txt", tab, strlen(tab));
    g_free(tab);
    g_setenv("YASDE_ICONE_KORA", dir, TRUE);

    PhServizi *s = servizi_prova();
    guint id = ph_srv_ascolta(s, PH_SRV_EV_ICONA, avviso_icona, NULL);
    GdkTexture *t = ph_srv_icona(s, "com.a");
    CONTROLLA(t != NULL && pixel(t) == VERDE); /* il primo pacchetto doppio vale */
    CONTROLLA(ph_srv_icona(s, "com.a") == t);  /* cache: la stessa texture */
    CONTROLLA(!chiesto("com.a"));              /* con Kora non si chiede al telefono */
    t = ph_srv_icona(s, "com.g");              /* niente a capo finale */
    CONTROLLA(t != NULL && pixel(t) == BLU);
    t = ph_srv_icona(s, "com.i");              /* \r\n e lato diverso: usata com'e' */
    CONTROLLA(t != NULL && gdk_texture_get_width(t) == 64);
    /* Come oggi: telefono (richiesta, NULL, poi l'icona arrivata). */
    const char *come_oggi[] = {"com.b", "com.c", "com.d", "com.e", "com.f", "com.h", "com.zzz", "malformata"};
    for (size_t i = 0; i < G_N_ELEMENTS(come_oggi); i++) {
        CONTROLLA(ph_srv_icona(s, come_oggi[i]) == NULL);
        CONTROLLA(chiesto(come_oggi[i]));
    }
    CONTROLLA(ph_srv_icona(s, lunga) == NULL);
    /* PNG fallito: non si rilegge (contratto), anche se ora sarebbe valido. */
    scrivi_png(dir, "rotto.png", 128, 0, 255, 0);
    CONTROLLA(ph_srv_icona(s, "com.h") == NULL);
    GBytes *png = png_colore(128, 255, 0, 0);
    ph_servizi_pubblica_icona(s, "com.zzz", g_bytes_get_data(png, NULL), g_bytes_get_size(png));
    g_bytes_unref(png);
    CONTROLLA(aspetta_int(&avvisi_icona, 1));
    t = ph_srv_icona(s, "com.zzz");
    CONTROLLA(t != NULL && pixel(t) == ROSSO);
    /* Kora vince sull'icona del telefono gia' arrivata. */
    png = png_colore(128, 255, 0, 0);
    ph_servizi_pubblica_icona(s, "com.a", g_bytes_get_data(png, NULL), g_bytes_get_size(png));
    g_bytes_unref(png);
    CONTROLLA(aspetta_int(&avvisi_icona, 2));
    t = ph_srv_icona(s, "com.a");
    CONTROLLA(t != NULL && pixel(t) == VERDE);
    ph_srv_smetti(s, id);
    ph_servizi_libera(s);

    /* Tabella vuota, tabella senza cartella, cartella senza tabella: come oggi. */
    scrivi(dir, "tabella.txt", "", 0);
    s = servizi_prova();
    CONTROLLA(ph_srv_icona(s, "com.a") == NULL && chiesto("com.a"));
    ph_servizi_libera(s);
    scrivi(dir, "tabella.txt", "###\n\n\n   \n", 10);
    s = servizi_prova();
    CONTROLLA(ph_srv_icona(s, "com.a") == NULL && chiesto("com.a"));
    ph_servizi_libera(s);
    char *p = g_build_filename(dir, "tabella.txt", NULL);
    g_remove(p);
    s = servizi_prova();
    CONTROLLA(ph_srv_icona(s, "com.a") == NULL && chiesto("com.a"));
    ph_servizi_libera(s);
    g_setenv("YASDE_ICONE_KORA", "/non/esiste/yasde", TRUE);
    s = servizi_prova();
    CONTROLLA(ph_srv_icona(s, "com.a") == NULL && chiesto("com.a"));
    ph_servizi_libera(s);

    /* FIFO al posto dei file: nessun blocco (se si blocca, SIGALRM uccide la prova). */
    g_setenv("YASDE_ICONE_KORA", dir, TRUE);
    alarm(20);
    CONTROLLA(mkfifo(p, 0600) == 0); /* tabella.txt e' una FIFO */
    s = servizi_prova();
    CONTROLLA(ph_srv_icona(s, "com.a") == NULL && chiesto("com.a"));
    ph_servizi_libera(s);
    g_remove(p);
    scrivi(dir, "tabella.txt", "com.a fifo\ncom.b verde\n", 23);
    char *pf = g_build_filename(dir, "fifo.png", NULL);
    CONTROLLA(mkfifo(pf, 0600) == 0); /* un PNG che e' una FIFO */
    s = servizi_prova();
    CONTROLLA(ph_srv_icona(s, "com.a") == NULL && chiesto("com.a"));
    t = ph_srv_icona(s, "com.b");
    CONTROLLA(t != NULL && pixel(t) == VERDE);
    ph_servizi_libera(s);
    alarm(0);
    g_free(pf);
    g_free(p);

    /* Tetti: al massimo 2000 righe. */
    GString *g = g_string_new(NULL);
    for (int i = 0; i < 3000; i++)
        g_string_append_printf(g, "p%d verde\n", i);
    scrivi(dir, "tabella.txt", g->str, g->len);
    s = servizi_prova();
    CONTROLLA(ph_srv_icona(s, "p0") != NULL);
    CONTROLLA(ph_srv_icona(s, "p1999") != NULL);
    CONTROLLA(ph_srv_icona(s, "p2500") == NULL);
    ph_servizi_libera(s);
    /* Oltre 256 KiB: il taglio cade dentro "com.t verdeX": la riga a meta' si scarta. */
    g_string_assign(g, "ok verde\n");
    riempi_commenti(g, 256 * 1024 - strlen("com.t verde"));
    g_string_append(g, "com.t verdeX\ncom.u verde\n");
    size_t lunghezza_oltre = g->len;
    scrivi(dir, "tabella.txt", g->str, g->len);
    s = servizi_prova();
    CONTROLLA(ph_srv_icona(s, "ok") != NULL);
    CONTROLLA(ph_srv_icona(s, "com.t") == NULL && chiesto("com.t"));
    CONTROLLA(ph_srv_icona(s, "com.u") == NULL);
    ph_servizi_libera(s);
    /* Controllo che la riga sia davvero raggiunta: lo stesso file, sotto il
     * tetto e con la riga intera, da' l'icona a com.t. */
    g_string_assign(g, "ok verde\n");
    riempi_commenti(g, 256 * 1024 - 100 - strlen("com.t verde\n"));
    g_string_append(g, "com.t verde\n");
    CONTROLLA(g->len < 256 * 1024 && g->len + 100 > 256 * 1024 - 100);
    scrivi(dir, "tabella.txt", g->str, g->len);
    s = servizi_prova();
    t = ph_srv_icona(s, "com.t");
    CONTROLLA(t != NULL && pixel(t) == VERDE);
    ph_servizi_libera(s);
    CONTROLLA(lunghezza_oltre > 256 * 1024);
    g_string_free(g, TRUE);

    /* Variabile non impostata e niente APPDIR: come oggi. */
    scrivi(dir, "tabella.txt", "com.a verde\n", 12);
    g_unsetenv("YASDE_ICONE_KORA");
    g_unsetenv("APPDIR");
    s = servizi_prova();
    CONTROLLA(ph_srv_icona(s, "com.a") == NULL && chiesto("com.a"));
    ph_servizi_libera(s);
    /* Variabile vuota: come non impostata. */
    g_setenv("YASDE_ICONE_KORA", "", TRUE);
    s = servizi_prova();
    CONTROLLA(ph_srv_icona(s, "com.a") == NULL && chiesto("com.a"));
    ph_servizi_libera(s);

    /* AppImage: solo APPDIR, file in usr/share/yasde/icone-kora. */
    g_unsetenv("YASDE_ICONE_KORA");
    char *appdir = g_build_filename(base, "appdir", NULL);
    char *ik = g_build_filename(appdir, "usr", "share", "yasde", "icone-kora", NULL);
    mkdir_p(ik);
    scrivi(ik, "tabella.txt", "com.a verde\n", 12);
    scrivi_png(ik, "verde.png", 128, 0, 255, 0);
    g_setenv("APPDIR", appdir, TRUE);
    s = servizi_prova();
    t = ph_srv_icona(s, "com.a");
    CONTROLLA(t != NULL && pixel(t) == VERDE && !chiesto("com.a"));
    ph_servizi_libera(s);
    g_unsetenv("APPDIR");
    g_free(ik);
    g_free(appdir);

    togli_cartella(base);
    g_free(dir);
    g_free(base);
    g_string_free(richieste, TRUE);
}

int main(void)
{
    ph_shell_segna_thread_gtk();
    CONTROLLA(ph_shell_nel_thread_gtk());

    /* Dal thread principale: mai dentro la chiamata. */
    ph_shell_consegna(ricevi, GINT_TO_POINTER(0), NULL);
    CONTROLLA(arrivati[0] == 0);
    GThread *t = g_thread_new("manda", manda, NULL);
    for (int i = 1; i < N; i++)
        ph_shell_consegna(ricevi, GINT_TO_POINTER(i), NULL);
    g_thread_join(t);
    gint64 fine = g_get_monotonic_time() + 5000000;
    while ((arrivati[0] < N || arrivati[1] < N) && g_get_monotonic_time() < fine)
        g_main_context_iteration(NULL, TRUE);
    CONTROLLA(arrivati[0] == N && arrivati[1] == N);
    CONTROLLA(fuori_ordine == 0);
    CONTROLLA(fuori_thread == 0);
    CONTROLLA(liberati == N);

    /* Orologio monotono. */
    uint64_t a = ph_shell_ora_ms();
    g_usleep(5000);
    CONTROLLA(ph_shell_ora_ms() >= a + 4);

    ph_shell_decisione("prova del registro", "nessuna: e' una prova (%d)", 42);

    /* Lo stile di base si legge senza errori di sintassi. */
    ph_shell_comune_risorse();
    CONTROLLA(ph_shell_stile_controlla("/it/yasde/shell/base.css") == 0);
    CONTROLLA(ph_shell_stile_controlla("/it/yasde/shell/non-esiste.css") != 0);

    /* Ogni tracciato delle icone del mockup si legge (gsk_path_parse non
     * richiede uno schermo). */
    const PhShellIcona *icone[] = {&PH_ICONA_SCHERMO_INTERO, &PH_ICONA_RIDUCI, &PH_ICONA_INGRANDISCI_HOST,
                                   &PH_ICONA_RIPRISTINA_HOST, &PH_ICONA_CHIUDI_HOST, &PH_ICONA_INDIETRO,
                                   &PH_ICONA_SCREENSHOT, &PH_ICONA_REGISTRA, &PH_ICONA_ALTRI,
                                   &PH_ICONA_RIDUCI_FINESTRA, &PH_ICONA_INGRANDISCI_FINESTRA,
                                   &PH_ICONA_CHIUDI_FINESTRA, &PH_ICONA_LAUNCHER, &PH_ICONA_INVIA,
                                   &PH_ICONA_SCARICA, &PH_ICONA_VOLUME, &PH_ICONA_VOLUME_MUTO, &PH_ICONA_WIFI,
                                   &PH_ICONA_BATTERIA, &PH_ICONA_CAMPANELLA, &PH_ICONA_LUCCHETTO};
    for (size_t i = 0; i < G_N_ELEMENTS(icone); i++) {
        const char *t[2] = {icone[i]->tratto, icone[i]->pieno};
        CONTROLLA(t[0] != NULL || t[1] != NULL);
        for (int k = 0; k < 2; k++) {
            if (t[k] == NULL)
                continue;
            GskPath *p = gsk_path_parse(t[k]);
            if (p == NULL)
                fprintf(stderr, "icona %zu: tracciato non valido: %s\n", i, t[k]);
            CONTROLLA(p != NULL && !gsk_path_is_empty(p));
            if (p != NULL)
                gsk_path_unref(p);
        }
    }

    /* Spigoli vivi: SOLO i sette simboli del titolo delle finestre interne; tutte le altre icone restano tonde. */
    const PhShellIcona *vivi[] = {&PH_ICONA_INDIETRO,        &PH_ICONA_SCREENSHOT,
                                  &PH_ICONA_REGISTRA,        &PH_ICONA_ALTRI,
                                  &PH_ICONA_RIDUCI_FINESTRA, &PH_ICONA_INGRANDISCI_FINESTRA,
                                  &PH_ICONA_CHIUDI_FINESTRA};
    for (size_t i = 0; i < G_N_ELEMENTS(icone); i++) {
        bool atteso = false;
        for (size_t k = 0; k < G_N_ELEMENTS(vivi); k++)
            atteso = atteso || icone[i] == vivi[k];
        CONTROLLA(icone[i]->spigoli_vivi == atteso);
    }

    /* Regola della prima connessione (shell.h). */
    CONTROLLA(ph_shell_connessione_da_mostrare(PH_SRV_NON_ABBINATO, true));
    CONTROLLA(ph_shell_connessione_da_mostrare(PH_SRV_NON_ABBINATO, false));
    CONTROLLA(ph_shell_connessione_da_mostrare(PH_SRV_CERCO, false));
    CONTROLLA(!ph_shell_connessione_da_mostrare(PH_SRV_CERCO, true));
    CONTROLLA(ph_shell_connessione_da_mostrare(PH_SRV_PERSO, false));
    CONTROLLA(!ph_shell_connessione_da_mostrare(PH_SRV_PERSO, true));
    CONTROLLA(!ph_shell_connessione_da_mostrare(PH_SRV_COLLEGATO, false));
    CONTROLLA(!ph_shell_connessione_da_mostrare(PH_SRV_BLOCCATO, false));
    CONTROLLA(!ph_shell_connessione_da_mostrare(PH_SRV_CHIUSO, false));

    prova_icone_kora();

    if (errori == 0)
        printf("prova_comune: tutto bene\n");
    return errori == 0 ? 0 : 1;
}
