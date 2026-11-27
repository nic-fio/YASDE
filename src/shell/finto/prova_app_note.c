/* Prova dell'insieme di app SOLO demo (opzione app_note di PhFintoOpzioni, usato dalle
 * schermate dei manuali): ogni app deve essere nella tabella vera data/icone-kora.txt
 * (passata come argomento), altrimenti la schermata mostrerebbe un'icona che il programma
 * vero non mostra. Senza schermo: solo il ciclo di GLib. */
#include <glib.h>
#include <stdio.h>
#include <string.h>

#include "finto.h"
#include "shell/servizi.h"
#include "shell/servizi_backend.h"

static int errori;
#define CONTROLLA(c)                                                                               \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            fprintf(stderr, "%s:%d: fallito: %s\n", __FILE__, __LINE__, #c);                       \
            errori++;                                                                              \
        }                                                                                          \
    } while (0)

/* Il pacchetto e' una riga «<pacchetto> <icona>» della tabella? */
static bool in_tabella(const char *tabella, const char *pacchetto)
{
    size_t n = strlen(pacchetto);
    for (const char *r = tabella; r != NULL && *r != '\0';) {
        if (strncmp(r, pacchetto, n) == 0 && r[n] == ' ')
            return true;
        r = strchr(r, '\n');
        if (r != NULL)
            r++;
    }
    return false;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "uso: %s <icone-kora.txt>\n", argv[0]);
        return 2;
    }
    gchar *tabella = NULL;
    if (!g_file_get_contents(argv[1], &tabella, NULL, NULL)) {
        fprintf(stderr, "tabella non leggibile: %s\n", argv[1]);
        return 1;
    }
    CONTROLLA(in_tabella(tabella, "com.whatsapp"));
    CONTROLLA(!in_tabella(tabella, "it.finto.note")); /* la ricerca distingue davvero */

    PhFintoOpzioni o = {.notifiche_ogni_s = -1, .ritardo_ms = 10, .app_note = true};
    PhFinto *f = ph_finto_nuovo(&o);
    CONTROLLA(f != NULL);
    if (f == NULL)
        return 1;
    PhServizi *s = ph_finto_servizi(f);
    size_t n = 0;
    gint64 fine = g_get_monotonic_time() + 3000 * 1000;
    while (ph_srv_app(s, &n), n == 0 && g_get_monotonic_time() < fine) {
        g_main_context_iteration(NULL, FALSE);
        g_usleep(1000);
    }
    const PhSrvApp *v = ph_srv_app(s, &n);
    CONTROLLA(n >= 12);
    for (size_t i = 0; i < n; i++) {
        if (!in_tabella(tabella, v[i].pacchetto)) {
            fprintf(stderr, "app non presente in icone-kora.txt: %s\n", v[i].pacchetto);
            errori++;
        }
        CONTROLLA(strncmp(v[i].pacchetto, "it.finto.", 9) != 0);
    }
    /* Il launcher "di sistema" (Play Store) e' in tabella: lo si vuole mostrare con l'icona vera. */
    CONTROLLA(ph_srv_app_cerca(s, "com.android.vending") != NULL && in_tabella(tabella, "com.android.vending"));
    size_t nn = 0;
    const char *problema = NULL;
    gint64 fine2 = g_get_monotonic_time() + 3000 * 1000;
    while (ph_srv_notifiche(s, &nn, &problema), nn == 0 && g_get_monotonic_time() < fine2) {
        g_main_context_iteration(NULL, FALSE);
        g_usleep(1000);
    }
    const PhSrvNotifica *nt = ph_srv_notifiche(s, &nn, &problema);
    CONTROLLA(nn == 3);
    for (size_t i = 0; i < nn; i++)
        CONTROLLA(in_tabella(tabella, nt[i].pacchetto));
    g_free(tabella);
    ph_finto_libera(f);
    return errori == 0 ? 0 : 1;
}
