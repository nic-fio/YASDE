/* Coda dei trasferimenti di file PC <-> telefono. Specifica 5.9.1, 5.9.2, 5.9.3.
 * Senza grafica: l'interfaccia accoda, annulla e legge lo stato; la coda ha un
 * solo thread di lavoro e fa un trasferimento alla volta, in ordine.
 * Usa solo file/file.h (copia, info) e adb/adb.h (ph_shell_esegui, ph_adb_viva).
 *
 * Thread:
 *  - tutte le funzioni ph_tr_* si possono chiamare da qualunque thread (anche
 *    insieme), tranne ph_tr_chiudi, che va chiamata una sola volta e quando
 *    nessun altro thread sta usando la coda;
 *  - l'avviso (PhTrAvviso) arriva SEMPRE dal thread di lavoro, mai dal thread
 *    di chi chiama. Cio' che fa chi chiama (accoda, annulla una voce in
 *    attesa, svuota) NON genera avvisi: chi chiama lo sa gia' e rilegge lo
 *    stato con ph_tr_elenca. Un annullamento di una voce in corso invece
 *    finisce con l'avviso "annullato" dal thread di lavoro;
 *  - nell'avviso si puo' chiamare ph_tr_elenca, ph_tr_in_corso, ph_tr_accoda,
 *    ph_tr_annulla, ph_tr_annulla_tutto e ph_tr_rimuovi_finiti (anche per la
 *    voce di cui arriva l'avviso finale); NON ph_tr_chiudi, ph_tr_riprendi e
 *    ph_tr_svuota (aspettano la fine dell'avviso: blocco assicurato).
 *
 * Regola del nome libero (mai sovrascrivere):
 *  - se la destinazione "cartella/nome" esiste gia' si prova "nome (1).ext",
 *    "nome (2).ext", ... fino a trovarne uno libero;
 *  - estensione = dall'ultimo punto in poi, ma solo se il punto non e' il primo
 *    carattere, dopo il punto c'e' almeno un carattere, ne seguono al massimo
 *    8 e sono tutti visibili (niente spazi o caratteri di controllo): cosi'
 *    "foto 1.5 finale" e ".bashrc" non hanno estensione e "Makefile" neppure
 *    ("Makefile (1)"; ".bashrc (1)");
 *  - estensioni doppie: se l'estensione e' di compressione (gz, bz2, xz, zst,
 *    lz, lzma, lz4, br, Z; maiuscole o minuscole) e subito prima c'e' ".tar",
 *    l'estensione e' ".tar.gz" ecc.: "a.tar.gz" -> "a (1).tar.gz";
 *  - il numero va prima dell'estensione, con uno spazio e fra parentesi;
 *    se il nome supera 255 byte si accorcia la parte prima del numero (senza
 *    spezzare un carattere UTF-8);
 *  - in ricezione un nome e' libero solo se lo sono sia "dest" sia
 *    "dest.parziale" (il file temporaneo di ph_file_ricevi, che non deve mai
 *    calpestare un file dell'utente; la coda non lo crea): il nome puo' avere
 *    al massimo 255 - 9 byte. Se dopo la scelta compare un ".parziale" o un
 *    file al posto della prenotazione, ph_file_ricevi risponde PH_FILE_ESISTE
 *    senza toccare nulla e la coda riprova con il numero successivo (fino a
 *    20 volte). La prenotazione si toglie solo se al nome c'e' ancora lo
 *    stesso file vuoto creato dalla coda (stessi dev/ino);
 *  - sul telefono "esiste" lo dice ph_file_info; sul PC lstat, e al momento
 *    di partire il nome si prenota con creazione esclusiva (O_EXCL) di un file
 *    vuoto che il file ricevuto sostituisce. Sul telefono tra il controllo e
 *    la scrittura resta una corsa (non esiste "crea solo se nuovo" in sync):
 *    e' ridotta al minimo scegliendo il nome subito prima di inviare;
 *  - due voci della coda non scelgono mai la stessa destinazione (sul telefono
 *    il confronto ignora maiuscole/minuscole, come il suo archivio), anche se
 *    una non e' ancora partita: ogni voce mostra subito un nome provvisorio
 *    che al via puo' solo salire di numero. */
#ifndef PH_TRASFERIMENTI_H
#define PH_TRASFERIMENTI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "adb/adb.h"
#include "comune/errore.h"

typedef enum {
    PH_TR_INVIO = 0,   /* PC -> telefono */
    PH_TR_RICEZIONE    /* telefono -> PC */
} PhTrVerso;

typedef enum {
    PH_TR_ATTESA = 0,
    PH_TR_CORSO,
    PH_TR_COMPLETATO,
    PH_TR_FALLITO,     /* errore = la frase da mostrare */
    PH_TR_ANNULLATO
} PhTrStato;

/* Una voce. Tutte le stringhe sono sempre non NULL ("" se non c'e' nulla). */
typedef struct {
    uint64_t id;            /* da 1, crescente, mai riusato */
    PhTrVerso verso;
    PhTrStato stato;
    char *nome;             /* nome finale del file (ultimo pezzo di destinazione) */
    char *origine;          /* invio: percorso sul PC; ricezione: percorso sul telefono */
    char *destinazione;     /* percorso completo di arrivo (telefono: /storage/emulated/0/...) */
    char *errore;           /* solo se FALLITO */
    uint64_t dimensione;    /* byte totali; in ricezione 0 finche' il telefono non risponde */
    uint64_t fatti;         /* byte copiati */
} PhTrVoce;

typedef struct {
    PhTrVoce *voci;         /* nell'ordine di accodamento */
    size_t numero;
} PhTrElenco;

/* Chiamata dal thread di lavoro a ogni cambio di stato (sempre) e durante la
 * copia con al massimo ~10 avvisi al secondo per voce. L'ultimo stato di ogni
 * voce (completato, fallito, annullato, con fatti aggiornati) arriva sempre.
 * *voce vale solo durante la chiamata.
 * voce == NULL significa "la coda si e' fermata" (connessione caduta o
 * assente): chi sta sopra legge ph_tr_coda_ferma e decide se riprendere o
 * svuotare. Se la ferma e' dovuta al fallimento di una voce, prima arriva
 * l'avviso FALLITO di quella voce, poi quello della coda.
 * ph_tr_coda_ferma diventa true PRIMA che arrivi l'avviso con voce NULL:
 * vederla true non significa che l'avviso sia gia' arrivato o finito. */
typedef void (*PhTrAvviso)(const PhTrVoce *voce, void *utente);

typedef struct PhTrasferimenti PhTrasferimenti;

/* Crea la coda e il suo thread. adb e' la connessione da usare (la coda non la
 * chiude mai). avviso e' facoltativo. NULL = errore. */
PhTrasferimenti *ph_tr_crea(PhAdb *adb, PhTrAvviso avviso, void *utente, PhErrore *e);

/* Ferma il thread, annulla il trasferimento in corso (il parziale sparisce),
 * scarta le voci rimaste e libera tutto. Dopo il ritorno non arrivano piu'
 * avvisi e la PhAdb data alla coda si puo' chiudere. Va bene anche con NULL.
 * Non chiamarla dall'avviso. */
void ph_tr_chiudi(PhTrasferimenti *t);

/* Accoda un trasferimento. Non blocca e non parla col telefono.
 *  - PH_TR_INVIO: origine = file del PC (deve essere un file normale),
 *    cartella = cartella del telefono, qualunque percorso assoluto; "/sdcard"
 *    vale "/storage/emulated/0". La cartella, se manca, la crea il telefono.
 *  - PH_TR_RICEZIONE: origine = file del telefono, cartella = cartella del PC,
 *    che deve esistere (errore chiaro se no).
 * Il nome del file e' l'ultimo pezzo di origine. *id (facoltativo) riceve l'id.
 * Per N file trascinati si chiama N volte: i nomi liberi sono distinti anche
 * tra voci non ancora partite, anche se l'origine e' la stessa. 0 / -1. */
int ph_tr_accoda(PhTrasferimenti *t, PhTrVerso verso, const char *origine, const char *cartella,
                 uint64_t *id, PhErrore *e);

/* Annulla una voce. In attesa: passa subito ad ANNULLATO e non partira'. In
 * corso: si interrompe la copia (il file parziale sparisce) e la voce diventa
 * ANNULLATO quando la copia si ferma; se la copia era gia' finita puo'
 * invece risultare COMPLETATO (0 non garantisce l'annullamento: guardare lo
 * stato finale). Gia' terminata o id sconosciuto: -1. */
int ph_tr_annulla(PhTrasferimenti *t, uint64_t id, PhErrore *e);

/* Annulla tutte le voci in attesa e quella in corso. */
void ph_tr_annulla_tutto(PhTrasferimenti *t);

/* Copia coerente (istantanea) di tutte le voci. Liberare con
 * ph_tr_elenco_libera. 0 / -1 (memoria). */
int ph_tr_elenca(PhTrasferimenti *t, PhTrElenco *elenco, PhErrore *e);
void ph_tr_elenco_libera(PhTrElenco *elenco);

/* true se c'e' una voce in corso o in attesa di partire: il "puntino colorato"
 * della barra. Con la coda ferma (vedi sotto) le voci in attesa non contano. */
bool ph_tr_in_corso(PhTrasferimenti *t);

/* Connessione caduta: la voce in corso fallisce con la sua frase, le altre
 * restano in attesa e la coda SI FERMA (anche se alla partenza di una voce la
 * connessione risulta gia' morta o manca): avviso con voce NULL e
 * ph_tr_coda_ferma() true. Resta ferma finche' chi sta sopra non chiama
 * ph_tr_riprendi o ph_tr_svuota (la ferma e' visibile prima dell'avviso, non
 * dopo). Accodare a coda ferma e' lecito e non tocca
 * nulla. */
bool ph_tr_coda_ferma(PhTrasferimenti *t);

/* Vita della PhAdb: il thread di lavoro usa la connessione dall'inizio di una
 * voce alla fine dell'eventuale avviso al media scanner. Le funzioni sotto
 * aspettano che la vecchia connessione non sia piu' in uso: al loro ritorno,
 * come al ritorno di ph_tr_chiudi, la vecchia PhAdb si puo' chiudere. Non
 * chiamarle dall'avviso: lo riconoscono e tornano -1 con un errore, senza
 * aspettare (altrimenti sarebbe uno stallo). Se la coda sta lavorando su una
 * connessione viva aspettano la fine dell'INTERA voce in corso (anche minuti):
 * non chiamarle dal thread dell'interfaccia se ph_tr_in_corso() e' vero e la
 * connessione e' viva; a coda ferma o inattiva tornano subito. Tornano 0 / -1.
 *
 * ph_tr_riprendi(nuova): riparte con la nuova connessione (le voci in attesa
 * continuano in ordine). ph_tr_riprendi(t, NULL) "stacca" la connessione: la
 * coda resta ferma senza connessione; serve quando la connessione cade a coda
 * inattiva e chi sta sopra vuole chiuderla subito. */
int ph_tr_riprendi(PhTrasferimenti *t, PhAdb *nuova, PhErrore *e);

/* Alternativa a riprendere: annulla tutto quello che e' in attesa E la voce in
 * corso e toglie dall'elenco le voci terminate. Se la coda era ferma stacca
 * anche la connessione (come ph_tr_riprendi(t, NULL): la coda resta ferma
 * finche' non si riprende con una nuova PhAdb); se non era ferma la
 * connessione resta e la coda continua a funzionare. Stesse regole sulla vita
 * della PhAdb di ph_tr_riprendi. */
int ph_tr_svuota(PhTrasferimenti *t, PhErrore *e);

/* Toglie dall'elenco le voci completate, fallite e annullate. */
void ph_tr_rimuovi_finiti(PhTrasferimenti *t);

/* ---- Funzioni pure, esposte perche' le provano i test e le usa l'interfaccia ---- */

/* Percorso del telefono pulito: deve essere assoluto; "/sdcard" diventa
 * "/storage/emulated/0"; "//" e "/" finale tolti. 0 / -1. */
int ph_tr_percorso_telefono(const char *in, char *out, size_t capacita, PhErrore *e);

/* Il nome con il numero della regola del nome libero (n == 0: invariato).
 * 0 / -1 (spazio insufficiente o nome vuoto). */
int ph_tr_nome_con_indice(const char *nome, unsigned n, char *out, size_t capacita);

/* Il comando di shell per avvisare il media scanner di Android che esiste un
 * nuovo file: am broadcast -a android.intent.action.MEDIA_SCANNER_SCAN_FILE
 * -d '<file:// con il percorso codificato %XX>'. Il percorso (assoluto, del
 * telefono) e' codificato in percentuale e poi messo tra apici singoli: nessun
 * carattere del nome puo' uscire dagli apici. 0 / -1. */
int ph_tr_comando_scanner(const char *percorso, char *out, size_t capacita);

#endif
