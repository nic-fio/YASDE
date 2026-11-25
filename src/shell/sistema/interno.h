/* Interno di shell-sistema: lo stato condiviso dai file del pezzo (notifiche,
 * toast, telefono, volume, pulsanti della barra). Non si include dall'esterno
 * (CONTRATTI.md: nessun pezzo include l'header interno di un altro). */
#ifndef PH_SHELL_SISTEMA_INTERNO_H
#define PH_SHELL_SISTEMA_INTERNO_H

#include "config/lingua.h"
#include "shell/pezzi.h"
#include "shell/sistema/logica.h"

typedef struct PhSistema PhSistema;

struct PhSistema {
    PhShell *sh;
    PhServizi *srv;
    bool chiuso; /* la radice e' in distruzione: nessun lavoro nuovo */

    guint id_srv[PH_SRV_EV_NUMERO];
    guint id_popup, id_finestre;

    /* Pulsanti per la barra (pulsanti.c): elenco dei vivi. */
    GPtrArray *pulsanti;

    /* Notifiche (notifiche.c) */
    PhNascoste *nascoste;
    PhViste *viste;
    GtkWidget *pannello_notifiche; /* il popup */
    GtkWidget *elenco_notifiche;   /* GtkBox con le voci */
    GtkWidget *btn_nascondi_tutte;
    char *firma_notifiche;         /* cio' che l'elenco mostra ora */
    size_t visibili;               /* contatore della campanella */

    /* Toast (toast.c) */
    GtkWidget *toast;
    GtkWidget *toast_titolo, *toast_testo;
    guint toast_timer;
    char *toast_chiave, *toast_pacchetto; /* NULL per i toast di informazione */
    uint64_t toast_ora_ms;

    /* Telefono (telefono.c) */
    GtkWidget *pannello_telefono;
    GtkWidget *riga_stato;
    GtkWidget *btn_schermo, *pill_schermo;
    GtkWidget *btn_pannello, *pill_pannello;
    PhPannelloAttesa attesa;
    guint attesa_timer;
    bool attesa_senza_esito;      /* il backend non sa dire l'esito: vale il tempo */
    GCancellable *attesa_annulla; /* richiesta del pannello in corso */

    /* Volume (volume.c) */
    GtkWidget *pannello_volume;
    GtkWidget *volume_valore, *volume_cursore;
    GtkWidget *btn_muto, *pill_muto;
    int volume;           /* quello che l'utente vede ora */
    bool muto;
    bool in_aggiornamento; /* il cursore e' mosso dal programma, non dall'utente */
    guint volume_timer;    /* a cursore fermo: salva e riallinea */
};

/* notifiche.c */
void ph_sistema_notifiche_inizia(PhSistema *s);
void ph_sistema_notifiche_ferma(PhSistema *s);
void ph_sistema_notifiche_cambiate(PhSistema *s);   /* PH_SRV_EV_NOTIFICHE */
void ph_sistema_notifiche_icona(PhSistema *s, const char *pacchetto); /* PH_SRV_EV_ICONA */
void ph_sistema_notifiche_profilo(PhSistema *s);    /* PH_SRV_EV_PROFILO */
bool ph_sistema_notifica_nascosta(void *io, const char *chiave, uint64_t ora_ms);
/* Nasconde (chiave, ora) e avvisa PH_SHELL_EV_NASCOSTE. */
void ph_sistema_notifica_nascondi(PhSistema *s, const char *chiave, uint64_t ora_ms);
void ph_sistema_notifiche_alterna(PhSistema *s);    /* apre / chiude il popup */
/* Ridisegna l'elenco e il contatore dallo stato attuale (anche se le app sono cambiate). */
void ph_sistema_notifiche_ridisegna(PhSistema *s);

/* toast.c */
void ph_sistema_toast_inizia(PhSistema *s);
void ph_sistema_toast_ferma(PhSistema *s);
/* Toast di informazione (3,2 s, il clic lo chiude soltanto). */
void ph_sistema_toast_info(void *io, const char *titolo, const char *testo);
/* Come ph_sistema_toast_info, ma resta in vista 9 s (avvisi con un'indicazione da leggere). */
void ph_sistema_toast_info_lungo(void *io, const char *titolo, const char *testo);
/* Toast di una notifica nuova (5,2 s, il clic apre l'app e nasconde la notifica). */
void ph_sistema_toast_notifica(PhSistema *s, const PhSrvNotifica *n);
/* Notifica nuova anche al desktop del PC (preferenza avvisi, host non in primo piano). */
void ph_sistema_notifica_al_desktop(PhSistema *s, const PhSrvNotifica *n);

/* telefono.c */
void ph_sistema_telefono_inizia(PhSistema *s);
void ph_sistema_telefono_ferma(PhSistema *s);
void ph_sistema_telefono_aggiorna(PhSistema *s);
void ph_sistema_telefono_alterna(PhSistema *s);

/* volume.c */
void ph_sistema_volume_inizia(PhSistema *s);
void ph_sistema_volume_ferma(PhSistema *s);
void ph_sistema_volume_aggiorna(PhSistema *s);
void ph_sistema_volume_alterna(PhSistema *s);

/* pulsanti.c */
void ph_sistema_pulsanti_inizia(PhSistema *s);
void ph_sistema_pulsanti_ferma(PhSistema *s);
GtkWidget *ph_sistema_pulsante(void *io, PhShellPopup tipo);
/* Ridisegna tutti i pulsanti dallo stato attuale (servizi + sistema). */
void ph_sistema_pulsanti_aggiorna(PhSistema *s);

/* Popup di sistema: colonna con la classe dei pannelli, ruolo gruppo con etichetta
 * (gia' tradotta), nascosta, aggiunta a PH_STRATO_PANNELLI. */
GtkWidget *ph_sistema_pannello_nuovo(PhSistema *s, int spaziatura, const char *etichetta);

/* Colloca un popup in basso a destra (mockup: right/bottom). */
void ph_sistema_popup_colloca(GtkWidget *w, int dal_bordo_destro, int dal_basso, int larghezza);

#endif
