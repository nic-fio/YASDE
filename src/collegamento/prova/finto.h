/* Telefoni, rete e configurazione finti per provare il collegamento senza
 * nessun altro pezzo. Sostituisce le funzioni di scoperta, adb, abbinamento,
 * componente, dati e config che il collegamento chiama. Valori tutti finti.
 *
 * Il "mondo" ha fino a FINTO_MAX_TEL telefoni. Un telefono ha indirizzo e
 * porta di collegamento, seriale, modello e uno stato: acceso (raggiungibile),
 * abbinato, bloccato, componente funzionante. Ogni connessione, componente e
 * dati creati sono contati: a fine prova devono tornare a zero. */
#ifndef PH_COL_FINTO_H
#define PH_COL_FINTO_H

#include <stdbool.h>
#include <stddef.h>

#include "scoperta/scoperta.h"

#define FINTO_MAX_TEL 4

/* Azzera tutto: telefoni, contatori, ritardi, file telefoni.toml. */
void finto_reset(void);

/* Definisce il telefono i: acceso, abbinato, sbloccato, componente buono.
 * seriale NULL o "" = getprop vuoto (ripiego su android_id). */
void finto_tel(int i, const char *host, int porta, const char *seriale, const char *modello);
void finto_tel_acceso(int i, bool si);       /* raggiungibile; false = cade anche ogni connessione */
void finto_tel_abbinato(int i, bool si);
void finto_tel_revoca(int i);     /* il telefono dimentica la chiave del PC (non abbinato) */
void finto_tel_blocca_e_cade(int i);          /* blocco + caduta in UN'unica operazione atomica */
void finto_tel_bloccato(int i, bool si);     /* chiama l'avviso di dati come il componente vero */
void finto_tel_componente_vivo(int i, bool si); /* false = il componente muore (connessione viva) */
void finto_tel_componente_fallisce(int i, bool si); /* ph_componente_avvia fallisce */
void finto_tel_android_id(int i, const char *id);
void finto_tel_seriale(int i, const char *seriale);
void finto_tel_nome_mdns(int i, const char *nome); /* nome mDNS forzato (anche non coerente) */
void finto_tel_evento_video(int i);                /* evento video verso chi l'ha registrato */
void finto_tel_notifiche(int i);                   /* il componente avvisa "notifiche cambiate" */
/* I prossimi n ph_shell_esegui falliscono (come un tempo scaduto). */
void finto_shell_fallimenti(int n);
/* Il servizio di abbinamento compare o no nella scoperta (default si'). */
void finto_abbinamento_visibile(bool si);
/* ph_dati_chiudi rifiuta (come con una ricerca ancora aperta): torna -1, nulla toccato. */
void finto_dati_rifiuta(bool si);
/* Quanti ph_adb_collega sono falliti (tornati NULL) dall'ultimo reset. */
int finto_collega_fallite(void);

/* Che cosa vede la scoperta: i telefoni elencati (in quest'ordine) se accesi,
 * piu' un eventuale servizio di abbinamento. Per default nessuno. */
void finto_scoperta_telefoni(const int *indici, int n);
/* Nome mDNS: se false il nome e' "casuale" (non dice il seriale). */
void finto_scoperta_nomi(bool con_seriale);
void finto_scoperta_ritardo_ms(int ms);
int finto_scoperta_chiamate(void);

/* Ritardo dentro ph_adb_collega e ph_componente_avvia (a meta' tentativo). */
void finto_collega_ritardo_ms(int ms);
void finto_avvia_ritardo_ms(int ms);
int finto_collega_chiamate(void);
int finto_avvia_chiamate(void);
/* Istante (ms monotonici) di ogni chiamata a ph_adb_collega; copia fino a max. */
int finto_collega_istanti(long *ms, int max);

/* Abbinamento: il codice giusto e' "123456"; riuscito abbina il telefono i. */
void finto_abbinamento_telefono(int i);
int finto_abbinamento_chiamate(void);
/* Pausa dentro ph_abbinamento_esegui (abbinamento "in corso"). */
void finto_abbinamento_ritardo_ms(int ms);

/* Conteggio degli oggetti vivi e ordine di rilascio ("D" dati, "C"
 * componente, "A" connessione, per ogni chiusura, in sequenza). */
int finto_vivi_adb(void);
int finto_vivi_componenti(void);
int finto_vivi_dati(void);
int finto_vivi_chiavi(void);
/* Quante connessioni sono state create dall'ultimo reset (gli indirizzi si riusano). */
int finto_creati_adb(void);
void finto_ordine_rilascio(char *out, size_t cap);
/* Indirizzo dell'ultimo PhAdb creato (per confrontare le sessioni). */
const void *finto_ultimo_adb(void);

/* Chiave del PC: il file della chiave sopravvive tra un'istanza e l'altra
 * finche' non si chiama finto_reset. Quante volte e' stata CREATA. */
int finto_chiavi_create(void);
/* Quante voci ha telefoni.toml in memoria. */
int finto_noti_conta(void);
/* Con true ph_t() antepone "[en] " al testo (prova che ogni motivo passa dalla traduzione). */
void finto_lingua_inglese(bool si);

/* telefoni.toml in memoria. Copia della voce con quel seriale. */
typedef struct {
    char seriale[64], nome[128], indirizzo[128];
    long visto;
} FintoNoto;
void finto_noti_aggiungi(const char *seriale, const char *nome, const char *indirizzo, long visto);
bool finto_noti_cerca(const char *seriale, FintoNoto *fuori);
int finto_noti_salvataggi(void);
/* Cambia il campo visto di una voce (per provare che si aggiorna). */
void finto_noti_imposta_visto(const char *seriale, long visto);
void finto_noti_rifiuta_salvataggio(bool si);

#endif
