/* Prova senza interfaccia: gdk-pixbuf, con i caricatori del pacchetto, salva e
 * rilegge un'immagine nei formati degli sfondi (png, jpeg, webp). */
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <stdio.h>

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "uso: %s CARTELLA\n", argv[0]);
        return 2;
    }
    const char *formati[] = {"png", "jpeg", "webp"};
    int esito = 0;
    GdkPixbuf *p = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8, 8, 8);
    if (p == NULL) {
        fprintf(stderr, "pixbuf non creato\n");
        return 1;
    }
    gdk_pixbuf_fill(p, 0x336699ff);
    for (int i = 0; i < 3; i++) {
        char *f = g_strdup_printf("%s/prova.%s", argv[1], formati[i]);
        GError *err = NULL;
        if (!gdk_pixbuf_save(p, f, formati[i], &err, NULL)) {
            fprintf(stderr, "%s: salvataggio non riuscito: %s\n", formati[i], err ? err->message : "?");
            g_clear_error(&err);
            esito = 1;
        } else {
            GdkPixbuf *q = gdk_pixbuf_new_from_file(f, &err);
            if (q == NULL || gdk_pixbuf_get_width(q) != 8 || gdk_pixbuf_get_height(q) != 8) {
                fprintf(stderr, "%s: lettura non riuscita: %s\n", formati[i], err ? err->message : "misure sbagliate");
                g_clear_error(&err);
                esito = 1;
            } else {
                printf("%s: salvato e riletto\n", formati[i]);
            }
            if (q != NULL)
                g_object_unref(q);
        }
        g_free(f);
    }
    g_object_unref(p);
    return esito;
}
