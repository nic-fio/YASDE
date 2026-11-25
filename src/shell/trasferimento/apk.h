/* Conferma dell'utente prima di installare un APK trascinato sulla finestra
 * (5.9.1; decisione di chi coordina: un .apk NON si invia come file). Dialogo
 * del sistema (GtkAlertDialog), non bloccante: la risposta torna con la
 * funzione `fatto` sul thread GTK, anche se il dialogo viene chiuso (conferma
 * = false). Solo thread GTK. */
#ifndef PH_SHELL_TRASFERIMENTO_APK_H
#define PH_SHELL_TRASFERIMENTO_APK_H

#include <gtk/gtk.h>
#include <stdbool.h>

typedef void (*PhTrfApkFatto)(void *utente, bool conferma, const char *percorso);

/* finestra puo' essere NULL. `percorso` si copia. */
void ph_trf_apk_chiedi(GtkWindow *finestra, const char *percorso, PhTrfApkFatto fatto, void *utente);

#endif
