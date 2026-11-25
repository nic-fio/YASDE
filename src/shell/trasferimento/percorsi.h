/* Logica pura del pezzo trasferimento (F-09): percorsi del telefono e del PC,
 * nomi liberi, dimensioni, testo di una voce della coda, completati tra due
 * fotografie, nascosti, pagine, testi sicuri. Niente widget: si prova senza
 * schermo (prova_trasferimento.c). Tutto sul thread GTK (usa la lingua attiva). */
#ifndef PH_SHELL_TRASFERIMENTO_PERCORSI_H
#define PH_SHELL_TRASFERIMENTO_PERCORSI_H

#include <glib.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "shell/servizi.h"

/* Memoria interna del telefono: "/sdcard" ne e' la scorciatoia (5.9.4). */
#define PH_TRF_MEMORIA_INTERNA "/storage/emulated/0"
/* Un percorso piu' lungo di cosi' (byte) non si accetta: ne' digitato ne' composto. */
#define PH_TRF_PERCORSO_MAX 4096
/* Voci per pagina negli elenchi (5.9): oltre, "Mostra altri N". */
#define PH_TRF_PAGINA 200

/* ---- Percorsi ------------------------------------------------------------ */

/* Percorso digitato -> percorso del telefono pulito: spazi ai lati tolti,
 * "/" iniziale aggiunto, "/sdcard" (anche maiuscolo) = memoria interna, "//"
 * e "/." tolti, ".." risolto (mai sopra "/"), "/" finali tolti, vuoto = "/".
 * false (e out = "/") se NULL, UTF-8 non valido, caratteri di controllo,
 * piu' lungo di PH_TRF_PERCORSO_MAX o di cap. */
bool ph_trf_normalizza(const char *grezzo, char *out, size_t cap);

/* Come sopra per il PC: niente "/sdcard"; "~" e' la cartella personale; un
 * percorso relativo parte da `base` (cartella mostrata, puo' essere NULL). */
bool ph_trf_normalizza_pc(const char *grezzo, const char *base, char *out, size_t cap);

bool ph_trf_e_radice(const char *percorso);

/* Cartella che contiene `percorso` (gia' pulito). true se si e' saliti; false
 * se era gia' "/" (out = "/") o se non sta in cap. */
bool ph_trf_su(const char *percorso, char *out, size_t cap);

/* `nome` e' un solo pezzo di percorso valido (non vuoto, senza "/", ne' "." ne' ".."). */
bool ph_trf_nome_valido(const char *nome);

/* cartella + "/" + nome ("/" non raddoppiata alla radice). false se il nome
 * non e' valido o il risultato non sta in cap. */
bool ph_trf_unisci(const char *cartella, const char *nome, char *out, size_t cap);

/* Nome libero tra `esistenti` (maiuscole e minuscole non contano): `base`, poi
 * "base 2", "base 3", ... false se non sta in cap. */
bool ph_trf_nome_libero(const char *base, const char *const *esistenti, size_t n, char *out,
                        size_t cap);

/* ---- Nascosti e pagine ---------------------------------------------------- */

/* Il nome inizia con il punto (5.9: nascosti). */
bool ph_trf_nascosto(const char *nome);
/* Un pezzo qualunque del percorso e' nascosto (risultati della ricerca). */
bool ph_trf_percorso_nascosto(const char *percorso);
/* Quante voci mostrare dopo `pagine` pagine (mai oltre il totale). */
size_t ph_trf_pagina_visibili(size_t totale, size_t pagine);
/* Il numero di "Mostra altri N": quante voci aggiunge il prossimo clic (0 = finite). */
size_t ph_trf_pagina_prossime(size_t totale, size_t visibili);

/* ---- Testi ---------------------------------------------------------------- */

/* Copia sicura da mostrare: UTF-8 valido, controlli e invisibili di direzione
 * al posto di "?", al massimo max_byte byte (con "…" se tagliata). Da liberare
 * con g_free. NULL in ingresso = "". */
char *ph_trf_testo_sicuro(const char *grezzo, size_t max_byte);

/* "{numero} B/KB/MB/GB" come il mockup (fmtSize): da 1 MiB una cifra decimale
 * (virgola in italiano, punto in inglese), da 1 KiB arrotondato. */
void ph_trf_dimensione(uint64_t byte, char *out, size_t cap);

/* 0-100. dimensione 0 (ignota) = 0. */
unsigned ph_trf_percento(uint64_t fatti, uint64_t dimensione);
/* "{percento}%", "Completato", "In attesa", "Annullato", "Non riuscito". */
void ph_trf_stato_testo(PhSrvTrStato stato, unsigned percento, char *out, size_t cap);
/* Riga sotto la barra: "{dimensione} · {destinazione}" (FALLITO: la frase
 * d'errore al posto della destinazione; dimensione ignota: solo la destinazione). */
void ph_trf_meta_testo(const PhSrvTrasferimento *t, char *out, size_t cap);
/* Una voce e' ancora da fare (in attesa o in corso): ha la "x". */
bool ph_trf_attivo(PhSrvTrStato stato);

/* ---- Quali voci della coda mostrare ----------------------------------------------- */
/* Tra le voci di `verso` sceglie al piu' `max` righe: prima quelle in corso, poi
 * quelle in attesa (ordine di coda), poi le finite, la piu' recente prima.
 * Scrive gli indici in `out` (da `max` posti) nell'ordine dall'alto; torna quanti.
 * *attive_nascoste (puo' essere NULL) = voci ancora da fare che non ci stanno. */
size_t ph_trf_scegli_voci(const PhSrvTrasferimento *v, size_t n, PhSrvVerso verso, size_t max, size_t *out,
                          size_t *attive_nascoste);

/* ---- Completati tra due fotografie ----------------------------------------- */
typedef struct PhTrfFoto PhTrfFoto;
PhTrfFoto *ph_trf_foto_nuova(void);
void ph_trf_foto_libera(PhTrfFoto *f);
/* Confronta l'elenco nuovo con la fotografia (per id) e la sostituisce. Torna
 * un GArray di guint con gli indici di v passati a COMPLETATO dall'ultima volta
 * (un id mai visto gia' completato conta); da liberare con g_array_free(.., TRUE). */
GArray *ph_trf_foto_completati(PhTrfFoto *f, const PhSrvTrasferimento *v, size_t n);
/* Come sopra ma senza dire nulla (prima fotografia, telefono diverso). */
void ph_trf_foto_riprendi(PhTrfFoto *f, const PhSrvTrasferimento *v, size_t n);

#endif
