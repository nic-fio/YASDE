/* Logica pura di shell-sistema (F-06, F-07, F-08): niente widget, quindi si
 * prova senza schermo (prova_sistema.c). Il resto del pezzo la usa e basta.
 *
 * Titoli e testi delle notifiche arrivano dal telefono: qui vivono solo in
 * memoria, mai su disco e mai nei log. */
#ifndef PH_SHELL_SISTEMA_LOGICA_H
#define PH_SHELL_SISTEMA_LOGICA_H

#include <glib.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "shell/servizi.h"

/* ---- Testi ostili -------------------------------------------------------
 * Rende un testo qualunque (UTF-8 non valido, a capo, tabulazioni, caratteri
 * di controllo, separatori di riga, formattazione bidirezionale, lunghissimo)
 * adatto a una riga di interfaccia: i caratteri di controllo diventano uno
 * spazio (e gli spazi consecutivi uno solo); gli unici caratteri tolti sono i
 * controlli di direzione del testo (U+202A-202E, U+2066-2069, U+200E/F,
 * U+061C): gli altri invisibili (congiuntori, varianti) restano, servono alle
 * emoji composte. Gli estremi si ripuliscono e oltre max_caratteri il
 * testo finisce con "…" (compreso nel conto). Mai oltre capacita byte,
 * terminato; origine NULL = "". */
void ph_sistema_testo_sicuro(const char *origine, size_t max_caratteri, char *out,
                             size_t capacita);

/* ---- Notifiche nascoste (F-06: il "nascondi" vale solo nella shell) -----
 * Insieme chiave -> ora_ms delle notifiche nascoste. Una notifica e' nascosta
 * solo finche' la sua ora_ms e' quella con cui e' stata nascosta: se il
 * telefono la aggiorna (ora diversa) torna visibile. */
typedef struct PhNascoste PhNascoste;
PhNascoste *ph_nascoste_nuove(void);
void ph_nascoste_libera(PhNascoste *n);
size_t ph_nascoste_numero(const PhNascoste *n);
/* true se l'insieme e' cambiato. chiave NULL: ignorata (false). */
bool ph_nascoste_nascondi(PhNascoste *n, const char *chiave, uint64_t ora_ms);
bool ph_nascoste_e_nascosta(const PhNascoste *n, const char *chiave, uint64_t ora_ms);
/* Nasconde tutte le notifiche della fotografia; true se e' cambiato qualcosa. */
bool ph_nascoste_nascondi_tutte(PhNascoste *n, const PhSrvNotifica *v, size_t quante);
/* Dimentica le nascoste che non esistono piu' o che sono state aggiornate;
 * true se ha tolto qualcosa. Con leggibile = false (il telefono non puo'
 * leggere le notifiche: la fotografia e' vuota per un guasto, non perche'
 * non ce ne siano) non tocca niente. */
bool ph_nascoste_sincronizza(PhNascoste *n, const PhSrvNotifica *v, size_t quante, bool leggibile);
/* Quante notifiche della fotografia sono visibili (contatore della campanella). */
size_t ph_nascoste_visibili(const PhNascoste *n, const PhSrvNotifica *v, size_t quante);

/* ---- Notifiche nuove (toast) ---------------------------------------------
 * Ricorda le chiavi viste dal collegamento. La PRIMA fotografia dopo ogni
 * azzeramento e' solo la base: nessun toast per cio' che c'era gia'
 * (sarebbero avvisi spuri). Una notifica aggiornata (stessa chiave) non e'
 * nuova. Una fotografia illeggibile non cambia niente (nemmeno la base). */
typedef struct PhViste PhViste;
PhViste *ph_viste_nuove(void);
void ph_viste_libera(PhViste *v);
/* La prossima fotografia leggibile sara' la base. */
void ph_viste_azzera(PhViste *v);
bool ph_viste_ha_base(const PhViste *v);
/* Registra la fotografia. generazione e pronte vengono dai servizi
 * (ph_srv_notifiche_generazione / ph_srv_notifiche_pronte): una fotografia
 * conta solo se la prima lettura del collegamento attuale e' arrivata (pronte)
 * e la PRIMA con una generazione nuova e' la base, mai un'altra: quella di
 * prima di una caduta, o presente quando il collegamento cambia, non e' mai la
 * base. Il profilo caricato non e' un evento delle viste. Torna quante notifiche sono nuove; *indice_toast
 * (se non NULL) e' l'indice della piu' recente tra le nuove (a parita' di
 * ora la prima), SIZE_MAX se nessuna. */
size_t ph_viste_registra(PhViste *v, const PhSrvNotifica *notifiche, size_t quante, bool leggibile,
                         unsigned generazione, bool pronte, size_t *indice_toast);

/* ---- Riga di una notifica ------------------------------------------------ */
/* "HH:MM" locale (o nel fuso dato) di ora_ms; "--:--" se fuori campo. */
void ph_sistema_ora_formatta(uint64_t ora_ms, GTimeZone *fuso, char *out, size_t capacita);
void ph_sistema_ora_locale(uint64_t ora_ms, char *out, size_t capacita);
/* "{app} · {ora}" con il nome reso sicuro. */
void ph_sistema_app_ora(const char *nome_app, uint64_t ora_ms, GTimeZone *fuso, char *out,
                        size_t capacita);

/* ---- Stato del telefono (F-07) -------------------------------------------- */
/* Versione di Android dal livello API: 34 -> 14, 35 -> 15, 36 -> 16, poi +1 per
 * livello; sotto la 34 la corrispondenza storica; 0 se non nota. */
int ph_sistema_versione_android(int api);
int ph_sistema_batteria_limita(int percento);
/* Riempimento interno dell'icona della batteria (mockup: rettangolo da x=4, y=8,
 * alto 4, largo 11 * carica / 100): tracciato SVG da riempire, "" se vuota. */
void ph_sistema_batteria_pieno(int percento, char *out, size_t capacita);
const char *ph_sistema_rete_testo(PhSrvRete rete);
/* "Android {versione} · Batteria {percento}% · {rete}"; "Android {versione}" se
 * lo stato non e' ancora noto; "" se non c'e' un telefono. */
void ph_sistema_riga_telefono(const PhSrvCollegamento *c, const PhSrvTelefono *t, char *out,
                              size_t capacita);

/* Richiesta del pannello fisico. Il telefono puo' RIFIUTARE lo spegnimento
 * (bloccato, chiamata in corso, ripristino impossibile da mettere al sicuro) e il
 * contratto non ha un esito: l'unica prova e' che lo stato vero diventi quello
 * chiesto entro il tempo dato. Lo stato vero puo' arrivare prima o dopo: si
 * guarda solo lo stato, mai l'ordine degli eventi. */
#define PH_PANNELLO_ATTESA_MS 6000
typedef struct {
    bool in_attesa;
    bool richiesto; /* true = acceso */
    uint64_t scadenza_ms;
} PhPannelloAttesa;
typedef enum {
    PH_PANNELLO_NIENTE = 0, /* niente da dire (in attesa o nessuna richiesta) */
    PH_PANNELLO_FATTO,      /* lo stato vero e' quello chiesto */
    PH_PANNELLO_RIFIUTATO   /* scaduto il tempo senza il cambio */
} PhPannelloEsito;
void ph_pannello_richiedi(PhPannelloAttesa *a, bool acceso, uint64_t ora_ms);
/* Chiude l'attesa senza esito (telefono scollegato, esito vero arrivato). */
void ph_pannello_annulla(PhPannelloAttesa *a);
/* Da chiamare a ogni cambio di stato e alla scadenza; chiude l'attesa quando
 * torna FATTO o RIFIUTATO. */
PhPannelloEsito ph_pannello_controlla(PhPannelloAttesa *a, bool acceso_ora, uint64_t ora_ms);

/* ---- Volume (F-08) ---------------------------------------------------------- */
int ph_sistema_volume_limita(int volume);
/* Il mockup mostra "muto" se muto o volume 0. */
bool ph_sistema_volume_muto_mostrato(int volume, bool muto);
/* "Muto" o "{percento}%" (testo accanto a "Volume"). */
void ph_sistema_volume_testo(int volume, bool muto, char *out, size_t capacita);
/* Scrive il livello nel profilo (limitato a 0-100); true se e' cambiato, cioe'
 * se serve salvare. Il muto non si salva. */
bool ph_sistema_volume_nel_profilo(PhCfgTelefono *profilo, int volume);
/* "Volume: muto" o "Volume: {percento}%" (etichetta del pulsante). */
void ph_sistema_volume_etichetta(int volume, bool muto, char *out, size_t capacita);

#endif
