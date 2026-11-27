/* Persistenza e configurazione (specifica 2.4, 5.10 e le sezioni che dicono
 * che cosa si salva: 4.4, 4.6, 5.2.1-3, 5.8, 5.9.3). Parte pura: nessuna
 * grafica, nessun telefono. Dipende solo da src/comune.
 *
 * Sul disco del PC si scrive SOLO in due cartelle, rispettando XDG:
 *   ~/.config/YASDE/   (o $XDG_CONFIG_HOME/YASDE)
 *       preferenze.toml          preferenze generali
 *       telefoni.toml            telefoni conosciuti
 *       telefono-<seriale>.toml  un file per telefono
 *   ~/.cache/YASDE/    (o $XDG_CACHE_HOME/YASDE)   immagini dello sfondo
 * Una variabile XDG_* vuota o con un percorso non assoluto si ignora (come dice
 * la specifica XDG). Niente e' mai scritto sul telefono.
 *
 * FORMATO. Testo UTF-8, sottoinsieme di TOML descritto in toml.h: sezioni
 * [nome], elenchi come tabelle ripetute [[nome]], stringhe tra virgolette,
 * interi, decimali, true/false, commenti con '#'. Esempio di un file per
 * telefono (valori finti):
 *
 *   [sfondo]
 *   tipo = "sfumato"            # solido | sfumato | immagine
 *   colore_solido = "#1f2430"   # usato dal tipo "solido"
 *   colore_da = "#0e2638"       # usati dal tipo "sfumato"
 *   colore_a = "#1a625f"
 *   direzione = "diagonale"     # diagonale (135 gradi CSS) | verticale (180)
 *                               # | orizzontale (90) | diagonale-inversa (45)
 *   immagine = "sfondo-R5CT0000000.png"   # nome di un file in ~/.cache/YASDE
 *   adattamento = "riempi"      # riempi | adatta | allunga | centra
 *
 *   [[icona]]
 *   app = "com.example.note"
 *   colonna = 0
 *   riga = 1
 *
 *   [[widget]]
 *   tipo = "orologio"           # orologio | stato | notifiche | memoria |
 *                               # calendario | note | meteo | riproduzione
 *   destra = 16                 # px dal bordo destro
 *   alto = 16                   # px dall'alto
 *   dimensione = "medio"        # piccolo | medio | grande
 *   larghezza = 0               # px; 0 = non salvata: vale `dimensione`
 *   altezza = 0                 # px; come sopra
 *   stile = 0                   # 0-15, variante grafica
 *   opzione = 0                 # 0-255, dipende dallo stile
 *
 *   [audio]
 *   volume = 100                # 0-100 (l'ultimo livello; il muto non si salva)
 *
 *   [cartelle]
 *   invio = "/home/utente/Immagini"
 *   ricezione = "/home/utente/Scaricati"
 *
 *   [app]
 *   densita = 0                 # 0 = quella del telefono, altrimenti 72-960 dpi
 *
 *   [[fissata]]                 # app fissate sulla barra, nell'ordine del file
 *   app = "com.example.note"
 *
 *   [[apertura]]                # aperture delle app (App frequenti)
 *   app = "com.example.note"
 *   volte = 12
 *   ultima = 1700000000         # secondi Unix
 *
 *   [meteo]                     # citta' scelta a mano; sezione scritta solo se scelta
 *   nome = "Citta-Finta"
 *   dettaglio = "Regione, Paese"
 *   lat_centesimi = 4512       # gradi * 100 (interi: niente decimali dipendenti dalla lingua)
 *   lon_centesimi = 920
 *
 *   [[compatibilita]]           # app con la voce "Compatibilita'" attiva
 *   app = "com.example.vecchia"
 *
 *   [[finestra]]                # "riapri dove eri"
 *   app = "com.example.note"
 *   x = 120
 *   y = 80
 *   larghezza = 400
 *   altezza = 290
 *
 * preferenze.toml:
 *   [generale]
 *   lingua = "auto"             # auto | it | en: si legge e si IGNORA (l'app e' solo in inglese)
 *   esc_indietro = true
 *   avvisi = true
 *   cartella_ricevuti = ""      # valore di partenza; la cartella di ricezione
 *                               # del singolo telefono, quando esiste, vince
 *   widget_nascosti = false     # interruttore globale "nascondi tutti i widget"
 *   riapri_dove_eri = false     # opzionale (4.6)
 *   [shell]
 *   scala_automatica = true
 *   scala = 1.0                 # 0.5-4.0, se non automatica
 *
 * telefoni.toml: una tabella [[telefono]] per telefono, con seriale, nome,
 * indirizzo e visto (secondi Unix); servono a ricollegarsi.
 *
 * Lettura della citta' del meteo: nome vuoto o troppo lungo, dettaglio troppo lungo,
 * latitudine fuori +-9000 o longitudine fuori +-18000, chiavi mancanti o non intere =
 * citta' non scelta (tutto azzerato), senza far fallire il caricamento. Un testo non
 * UTF-8 nel file e' invece un errore di sintassi di tutto il file (come ovunque: file
 * messo da parte).
 *
 * ROBUSTEZZA. Un file rovinato non fa mai crashare. Regole di lettura:
 *  - errore di sintassi (virgolette non chiuse, numero enorme, byte nullo, UTF-8
 *    non valido, riga troppo lunga...): la funzione torna -1 con un messaggio
 *    "riga N: ..." e la struttura contiene i valori predefiniti (mai mezza
 *    struttura); chi chiama decide se avvisare l'utente;
 *  - tabelle e chiavi sconosciute: ignorate senza errore (e non riscritte al
 *    salvataggio);
 *  - valore di tipo sbagliato, fuori intervallo, troppo lungo o con un nome
 *    sconosciuto: quel campo torna al predefinito, senza errore; una voce di
 *    elenco con campi obbligatori non validi (o doppia) e' scartata;
 *  - file assente: valori predefiniti, ritorno 0 (primo avvio).
 * FILE ILLEGGIBILE. Le funzioni *_carica, se il file esiste ma non si legge
 * (sintassi sbagliata o troppo grande), lo mettono da parte rinominandolo
 * "<nome>.illeggibile" (sostituendo un eventuale precedente), restituiscono i
 * valori predefiniti e tornano 1 (il messaggio in PhErrore dice la riga e dove
 * e' finito il file): cosi' il salvataggio successivo non distrugge nulla.
 * Il nome e' ".illeggibile", poi ".illeggibile.1" ... ".illeggibile.9" se
 * esiste gia' (rename senza sostituire: due istanze non si schiacciano la
 * copia); esauriti i nomi si sostituisce il piu' vecchio.
 * Esiti di *_carica: 0 letto o assente, 1 messo da parte, -1 errore vero
 * (percorso, permessi, rename fallito). DOPO -1 NON SI DEVE SALVARE: il file
 * non e' stato ne' letto ne' messo da parte e il salvataggio lo distruggerebbe. Le funzioni *_da_testo non toccano il
 * disco: tornano -1 con la riga.
 * PREDEFINITI DEL TELEFONO. Al primo avvio (file assente o messo da parte) il
 * desktop ha tre widget (orologio, stato del telefono, notifiche recenti, medi,
 * a 16 px dal bordo destro e 16, 120, 240 px dall'alto, come nel mockup). Un
 * file valido ha sempre l'ultima parola: senza [[widget]] significa nessun
 * widget.
 *
 * SCRITTURA SICURA. File temporaneo nella stessa cartella (permessi 0600),
 * fsync, rename sul definitivo, fsync della cartella: un'interruzione non lascia
 * mai un file mezzo scritto, o resta il vecchio o c'e' il nuovo. Cartelle create
 * con 0700.
 *
 * NUMERO DI SERIALE. Arriva dal telefono, quindi e' dato ostile: nel nome del
 * file entra solo ripulito (ph_cfg_seriale_pulito); dentro telefoni.toml resta
 * com'e' (e' un valore, mai un percorso).
 *
 * LIMITI NOTI della scrittura:
 *  - se il file di configurazione e' un collegamento simbolico, il rename lo
 *    sostituisce con un file normale (il collegamento sparisce, il bersaglio
 *    resta com'era);
 *  - due istanze del programma insieme: vince l'ultima scrittura, mai un file
 *    mezzo scritto;
 *  - un'interruzione brusca puo' lasciare un temporaneo ".nome.XXXXXX":
 *    all'avvio si chiama ph_cfg_pulisci_temporanei(3600), che toglie quelli
 *    piu' vecchi di un'ora;
 *  - la cartella YASDE, se esiste, deve essere dell'utente, altrimenti
 *    si rifiuta di scriverci; se e' sua ma scrivibile da gruppo o altri (umask
 *    0002) toglie quei permessi e prosegue.
 *
 * Valore di ritorno, come nel resto del progetto: 0 riuscito, -1 errore (dove
 * non e' detto diversamente). */
#ifndef PH_CONFIG_H
#define PH_CONFIG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "comune/buffer.h"
#include "comune/errore.h"

/* Limiti (comprendono il terminatore delle stringhe). */
#define PH_CFG_LUN_SERIALE 64 /* campo e nome del file: stesso limite (63 caratteri) */
#define PH_CFG_LUN_SERIALE_FILE (PH_CFG_LUN_SERIALE + 9) /* 63 + "-" + 8 cifre + terminatore */
#define PH_CFG_LUN_NOME 128
#define PH_CFG_LUN_INDIRIZZO 128
#define PH_CFG_LUN_APP 192      /* nome del pacchetto Android */
#define PH_CFG_LUN_PERCORSO 1024
#define PH_CFG_LUN_FILE_CACHE 128
#define PH_CFG_LUN_NOTE 2048   /* testo delle note rapide: 2047 byte di UTF-8 + terminatore */
#define PH_CFG_MAX_TELEFONI 64
#define PH_CFG_MAX_ICONE 256
#define PH_CFG_MAX_WIDGET 32
#define PH_CFG_MAX_FINESTRE 64
#define PH_CFG_MAX_FISSATE 16 /* app fissate sulla barra */
#define PH_CFG_MAX_COMPAT 128 /* app in "Compatibilita'" */
#define PH_CFG_MAX_APERTURE 64 /* app con le aperture contate */
#define PH_CFG_MAX_VOLTE 1000000000u /* tetto del contatore: oltre non sale */
#define PH_CFG_MAX_FILE (1024u * 1024u) /* un file di configurazione piu' grande e' un errore */

/* ---------- percorsi ---------- */

/* Scrivono in out il percorso assoluto della cartella (senza crearla). */
int ph_cfg_cartella_config(char *out, size_t capacita, PhErrore *e);
int ph_cfg_cartella_cache(char *out, size_t capacita, PhErrore *e);

/* Ripulisce il numero di seriale per usarlo in un nome di file: tiene
 * [A-Za-z0-9_-], ogni altro byte diventa '_', al massimo 63 caratteri. Se
 * qualcosa e' stato cambiato o troncato si aggiunge "-" e l'impronta (FNV-1a a 32
 * bit, 8 cifre esadecimali) del seriale originale, cosi' "AB:CD" e "AB.CD" non
 * finiscono nello stesso file. Errore se il seriale e' vuoto o `capacita` e'
 * minore di PH_CFG_LUN_SERIALE_FILE. */
int ph_cfg_seriale_pulito(const char *seriale, char *out, size_t capacita, PhErrore *e);

/* Percorsi completi dei file di configurazione. */
int ph_cfg_percorso_preferenze(char *out, size_t capacita, PhErrore *e);
int ph_cfg_percorso_telefoni(char *out, size_t capacita, PhErrore *e);
int ph_cfg_percorso_telefono(const char *seriale, char *out, size_t capacita, PhErrore *e);
/* La chiave ADB del PC: <cartella config>/chiave.pem (il file lo gestisce chi
 * la usa, con ph_cfg_scrivi_file se vuole la scrittura sicura). */
int ph_cfg_percorso_chiave(char *out, size_t capacita, PhErrore *e);

/* Toglie dalle cartelle config e cache i temporanei ".nome.XXXXXX" (file
 * normali dell'utente) piu' vecchi di `secondi`. Da chiamare all'avvio con 3600 (un valore sotto 60 diventa 60).
 * Torna quanti ne ha tolti, -1 se non trova la cartella personale. */
int ph_cfg_pulisci_temporanei(long secondi, PhErrore *e);

/* Pulizia dei file di sfondo vecchi nella cache. Da chiamare UNA volta
 * all'avvio, prima di creare thread. Legge TUTTI i telefono-*.toml, raccoglie i
 * nomi in sfondo.immagine e toglie dalla cache i file che si chiamano ESATTAMENTE
 * "sfondo-<1-10 cifre>.png" e che nessun profilo nomina. Garanzie:
 *  - se anche un solo profilo non si legge (errore, troppo grande, sintassi
 *    sbagliata) non si cancella NULLA e torna -2;
 *  - un nome nel profilo che non e' un nome valido della cache (con '/', '..',
 *    ecc.) non e' un riferimento e non fa cancellare nulla altrove: si lavora
 *    solo dentro la cartella di cache, con unlinkat sul suo descrittore;
 *  - si toccano solo file normali dell'utente (mai cartelle, mai collegamenti
 *    simbolici: il collegamento resta, e il suo bersaglio mai);
 *  - un file modificato meno di 10 minuti fa resta (puo' essere di una scelta
 *    in corso non ancora salvata);
 *  - cartella di configurazione o di cache assente: non si fa nulla, torna 0;
 *    i file ".illeggibile" non contano come profili.
 *  - un errore nella lettura dell'elenco di una cartella (readdir) non e' la fine
 *    dell'elenco: torna -1; se succede sulla cartella di configurazione non si
 *    cancella nulla (la cache non viene nemmeno aperta).
 * PER CHI SALVA UNO SFONDO: scrivere l'immagine con ph_cfg_scrivi_file e salvare il
 * profilo che la nomina SUBITO dopo. Un'immagine piu' vecchia di 10 minuti che
 * nessun profilo nomina viene tolta anche se un'altra istanza dell'app la sta per
 * usare. Un profilo modificato a mano in modo che il lettore non riconosca piu' il
 * riferimento (nome non valido, tipo sbagliato, voce scartata) perde la sua
 * immagine alla pulizia successiva.
 * Torna quanti file ha tolto (>= 0), -1 errore (cartella non dell'utente, memoria,
 * oltre 1024 profili, errore di lettura di una cartella), -2 un profilo
 * illeggibile: niente e' stato cancellato. */
int ph_cfg_pulisci_sfondi(PhErrore *e);

/* Nome di un file nella cache valido (nessun '/', non inizia con '.', solo
 * [A-Za-z0-9._-], lunghezza 1-127): chi chiama lo controlla prima di
 * costruire percorsi; il lettore dei file lo applica da se'. */
bool ph_cfg_nome_cache_valido(const char *nome);
int ph_cfg_percorso_cache(const char *nome, char *out, size_t capacita, PhErrore *e);

/* ---------- file ---------- */

/* Scrive il file in modo sicuro (vedi sopra) creando le cartelle mancanti. */
int ph_cfg_scrivi_file(const char *percorso, const void *dati, size_t lunghezza, PhErrore *e);

/* Legge un file normale (non FIFO o dispositivi) fino a `massimo` byte.
 * Torna 0 letto, 1 il file non esiste (out vuoto), 2 troppo grande (out vuoto),
 * -1 altro errore. */
int ph_cfg_leggi_file(const char *percorso, size_t massimo, PhBuffer *out, PhErrore *e);

/* ---------- preferenze generali (preferenze.toml) ---------- */

typedef enum { PH_CFG_LINGUA_AUTO, PH_CFG_LINGUA_IT, PH_CFG_LINGUA_EN } PhCfgLingua;

typedef struct {
    PhCfgLingua lingua;   /* predefinito AUTO; IGNORATA: la lingua e' sempre l'inglese (lingua.h) */
    bool esc_indietro;    /* il tasto Esc vale "indietro"; predefinito true */
    bool avvisi;          /* avvisi a comparsa; predefinito true */
    char cartella_ricevuti[PH_CFG_LUN_PERCORSO]; /* "" = la sceglie la shell; e' il valore
                                                    di partenza: la cartella di ricezione
                                                    del telefono, se c'e', vince */
    bool widget_nascosti; /* interruttore globale "nascondi tutti i widget"; predefinito false */
    bool riapri_dove_eri; /* "riapri dove eri" (4.6, opzionale); predefinito false */
    bool scala_automatica; /* scala della shell dal sistema; predefinito true */
    double scala;          /* usata se !scala_automatica; 0.5-4.0, predefinito 1.0 */
} PhCfgPreferenze;

void ph_cfg_preferenze_predefinite(PhCfgPreferenze *p);
int ph_cfg_preferenze_da_testo(const char *testo, size_t lunghezza, PhCfgPreferenze *p, PhErrore *e);
int ph_cfg_preferenze_in_testo(const PhCfgPreferenze *p, PhBuffer *out);
int ph_cfg_preferenze_carica(PhCfgPreferenze *p, PhErrore *e);
int ph_cfg_preferenze_salva(const PhCfgPreferenze *p, PhErrore *e);

/* ---------- telefoni conosciuti (telefoni.toml) ---------- */

typedef struct {
    char seriale[PH_CFG_LUN_SERIALE]; /* obbligatorio e non vuoto; senza doppioni */
    char nome[PH_CFG_LUN_NOME];       /* nome/modello */
    char indirizzo[PH_CFG_LUN_INDIRIZZO]; /* ultimo indirizzo: "host:porta" (IPv6: "[host]:porta") */
    int64_t visto;                    /* ultima volta visto, secondi Unix (0 = mai) */
} PhCfgTelefonoNoto;

typedef struct {
    PhCfgTelefonoNoto voci[PH_CFG_MAX_TELEFONI];
    size_t n;
} PhCfgTelefoni;

void ph_cfg_telefoni_predefiniti(PhCfgTelefoni *t);
int ph_cfg_telefoni_da_testo(const char *testo, size_t lunghezza, PhCfgTelefoni *t, PhErrore *e);
int ph_cfg_telefoni_in_testo(const PhCfgTelefoni *t, PhBuffer *out);
int ph_cfg_telefoni_carica(PhCfgTelefoni *t, PhErrore *e);
int ph_cfg_telefoni_salva(const PhCfgTelefoni *t, PhErrore *e);

/* ---------- un telefono (telefono-<seriale>.toml) ---------- */

typedef enum { PH_CFG_SFONDO_SOLIDO, PH_CFG_SFONDO_SFUMATO, PH_CFG_SFONDO_IMMAGINE } PhCfgSfondoTipo;
/* Gli angoli CSS del mockup: 135, 180, 90, 45 gradi. */
typedef enum {
    PH_CFG_DIR_DIAGONALE,
    PH_CFG_DIR_VERTICALE,
    PH_CFG_DIR_ORIZZONTALE,
    PH_CFG_DIR_DIAGONALE_INVERSA
} PhCfgDirezione;
typedef enum { PH_CFG_ADATT_RIEMPI, PH_CFG_ADATT_ADATTA, PH_CFG_ADATT_ALLUNGA, PH_CFG_ADATT_CENTRA } PhCfgAdattamento;

typedef struct {
    PhCfgSfondoTipo tipo;     /* predefinito SFUMATO */
    uint32_t colore_solido;   /* 0xRRGGBB, per SOLIDO; predefinito 0x1f2430 (mockup) */
    uint32_t colore_da;       /* 0xRRGGBB, per SFUMATO; predefinito 0x0e2638 */
    uint32_t colore_a;        /* 0xRRGGBB; predefinito 0x1a625f */
    PhCfgDirezione direzione; /* predefinito DIAGONALE (135 gradi) */
    char immagine[PH_CFG_LUN_FILE_CACHE]; /* file nella cache, "" = nessuno */
    PhCfgAdattamento adattamento; /* predefinito RIEMPI */
} PhCfgSfondo;

typedef struct {
    char app[PH_CFG_LUN_APP];
    int colonna, riga; /* 0-255 */
} PhCfgIcona;

/* Tipi di widget del catalogo di 5.2.3. */
typedef enum {
    PH_CFG_WIDGET_OROLOGIO,
    PH_CFG_WIDGET_STATO,
    PH_CFG_WIDGET_NOTIFICHE,
    PH_CFG_WIDGET_MEMORIA,
    PH_CFG_WIDGET_CALENDARIO,
    PH_CFG_WIDGET_NOTE,
    PH_CFG_WIDGET_METEO,
    PH_CFG_WIDGET_RIPRODUZIONE,
    /* Aggiunti dopo: in coda, i valori salvati prima restano validi. */
    PH_CFG_WIDGET_CALENDARIO_MESE, /* "calendario-mese" */
    PH_CFG_WIDGET_SVEGLIA,         /* "sveglia" */
    PH_CFG_WIDGET_BATTERIA,        /* "batteria" */
    PH_CFG_WIDGET_INTERRUTTORI,    /* "interruttori" */
    PH_CFG_WIDGET_TROVA,           /* "trova" */
    PH_CFG_WIDGET_APP_FREQUENTI,   /* "app-frequenti" */
    PH_CFG_WIDGET_TRASFERIMENTI,   /* "trasferimenti" */
    PH_CFG_WIDGET_CHIAMATE,        /* "chiamate": chiamate recenti (2.3) */
    PH_CFG_WIDGET_RETE             /* "rete": Wi-Fi e rete mobile (2.3) */
} PhCfgWidgetTipo;
typedef enum { PH_CFG_WIDGET_PICCOLO, PH_CFG_WIDGET_MEDIO, PH_CFG_WIDGET_GRANDE } PhCfgWidgetDim;

typedef struct {
    PhCfgWidgetTipo tipo; /* obbligatorio; un tipo sconosciuto scarta la voce */
    int destra, alto;     /* px dal bordo destro e dall'alto, 0-16384; predefinito 16 */
    PhCfgWidgetDim dimensione; /* predefinito MEDIO */
    int larghezza, altezza; /* misura libera in px; chiavi "larghezza" e "altezza".
                               0 = non salvata (profili scritti prima delle misure
                               libere): chi disegna usa `dimensione`. Valori ammessi
                               0 oppure 48-2048; uno non intero o fuori intervallo torna
                               a 0 senza far fallire il caricamento. Si scrivono solo se
                               diversi da 0; `dimensione` si continua a scrivere. */
    int stile;   /* variante grafica del widget, 0-15, predefinito 0; chiave "stile" */
    int opzione; /* valore che dipende dallo stile (es. la citta' del doppio fuso),
                    0-255, predefinito 0; chiave "opzione". Come per gli altri campi,
                    un valore non intero o fuori intervallo torna a 0 senza far
                    fallire il caricamento; i file senza le chiavi danno 0. */
} PhCfgWidget;

typedef struct {
    char app[PH_CFG_LUN_APP]; /* una sola finestra salvata per app */
    int x, y;                 /* -32768..32767 */
    int larghezza, altezza;   /* 1-16384 */
} PhCfgFinestra;

/* Quante volte un'app e' stata aperta (widget "App frequenti"): solo pacchetto e
 * numeri, mai nomi o titoli. */
typedef struct {
    char app[PH_CFG_LUN_APP];
    uint32_t volte;  /* 1-PH_CFG_MAX_VOLTE */
    int64_t ultima;  /* ultima apertura, secondi Unix (0 = ignota) */
} PhCfgApertura;

typedef struct {
    PhCfgSfondo sfondo;
    PhCfgIcona icone[PH_CFG_MAX_ICONE]; /* una sola icona per app */
    size_t n_icone;
    PhCfgWidget widget[PH_CFG_MAX_WIDGET]; /* predefiniti: vedi PREDEFINITI DEL TELEFONO */
    size_t n_widget;
    int volume;           /* 0-100, predefinito 100; il muto non si salva (5.8) */
    char cartella_invio[PH_CFG_LUN_PERCORSO];     /* ultima usata, "" = nessuna */
    char cartella_ricezione[PH_CFG_LUN_PERCORSO];
    int densita_app;      /* 0 = quella del telefono, altrimenti 72-960 */
    char fissate[PH_CFG_MAX_FISSATE][PH_CFG_LUN_APP]; /* pacchetti fissati sulla barra, in ordine;
                                                         validi come le icone (non vuoti, entro
                                                         il limite), senza doppioni; file senza
                                                         [[fissata]] = nessuna */
    size_t n_fissate;
    PhCfgFinestra finestre[PH_CFG_MAX_FINESTRE];
    size_t n_finestre;
    PhCfgApertura aperture[PH_CFG_MAX_APERTURE]; /* file vecchi senza [[apertura]] = nessuna */
    size_t n_aperture;
    /* Citta' del widget Meteo scelta a mano dall'utente (una per telefono). E' l'UNICO
     * dato di luogo che va su disco: la posizione del telefono e il nome della sua
     * localita' non si salvano mai; il luogo non va mai nei log ne' nei messaggi
     * d'errore. Predefinito: non scelta, stringhe vuote, 0. */
    bool meteo_luogo_scelto;
    char meteo_luogo_nome[128];
    char meteo_luogo_dettaglio[128]; /* es. regione e paese; puo' essere vuoto */
    int32_t meteo_luogo_lat_centesimi; /* gradi * 100, -9000..9000 */
    int32_t meteo_luogo_lon_centesimi; /* gradi * 100, -18000..18000 */
    /* Note rapide (widget "note"): un solo testo per telefono, scritto dall'utente sul
     * PC; non viene dal telefono e non ci va. Chiave "testo" nella tabella [note],
     * scritta solo se il testo non e' vuoto. UTF-8 valido, a capo ammessi ('\n';
     * un '\r' letto si toglie), tabulazione ammessa, nessun altro carattere di
     * controllo ASCII (byte < 0x20, ne' 0x7F); i caratteri U+0080-U+009F non si
     * filtrano. In lettura: un carattere di controllo non ammesso arrivato come
     * escape (\\b, \\f, \\u00XX) -> testo vuoto; chiave di altro tipo -> testo
     * vuoto; piu' lungo del campo -> troncato a un confine di carattere. Come per
     * ogni altra stringa del profilo, UTF-8 non valido o un carattere di controllo
     * scritto in chiaro nel file sono un errore di sintassi dell'intero file (lo
     * rifiuta il lettore TOML): YASDE non scrive mai un file cosi': un testo
     * con un carattere di controllo non ammesso NON si scrive (la tabella [note]
     * si salta: alla rilettura e' vuoto, come lo darebbe la lettura). In scrittura
     * la stringa TOML ha gli escape necessari (\\, \", \n, \t) e un testo valido
     * si rilegge identico; chi mette il testo nel campo (la shell) lo da' gia'
     * valido: senza '\r', senza controlli, UTF-8 corretto. E' un dato
     * dell'utente: mai in un log ne' in un messaggio d'errore. Predefinito:
     * stringa vuota. */
    char note[PH_CFG_LUN_NOTE];
    /* App con la voce "Compatibilita'" attiva (si disegnano rimpicciolite): solo
     * pacchetti, in ordine, validi come le app fissate, senza doppioni; file senza
     * [[compatibilita]] = nessuna. */
    char compatibilita[PH_CFG_MAX_COMPAT][PH_CFG_LUN_APP];
    size_t n_compatibilita;
} PhCfgTelefono;

bool ph_cfg_telefono_in_compatibilita(const PhCfgTelefono *t, const char *app);
/* Aggiunge (attiva) o toglie l'app, mantenendo l'ordine delle altre. Gia' nello
 * stato chiesto: 0, nulla cambia. -1 con nome non valido (NULL, vuoto, troppo
 * lungo) o, aggiungendo, con l'elenco pieno (128): messaggio «Troppe app in
 * compatibilita'.» tradotto con ph_t; e' una scelta dell'utente, quindi NON si toglie
 * la voce piu' vecchia e nulla cambia. Da salvare SOLO quando l'utente cambia la voce. */
int ph_cfg_telefono_imposta_compatibilita(PhCfgTelefono *t, const char *app, bool attiva, PhErrore *e);

/* Conta un'apertura: +1 alla voce dell'app (saturando a PH_CFG_MAX_VOLTE) e
 * aggiorna `ultima` (ora = secondi Unix; fuori 0..anno 2100 vale 0), oppure
 * crea la voce con volte = 1. Elenco pieno (64): prima si toglie la voce con
 * MENO aperture e, a parita', quella con `ultima` piu' vecchia (la prima in
 * elenco se uguali). -1 se il nome non e' valido (NULL, vuoto, troppo lungo):
 * nulla cambia. In lettura: voci non valide (app o volte fuori misura) o doppie
 * scartate, ultima fuori intervallo = 0, oltre 64 voci troncato. */
int ph_cfg_telefono_conta_apertura(PhCfgTelefono *t, const char *app, int64_t ora, PhErrore *e);

/* App fissate sulla barra, sul profilo in memoria (il salvataggio e' a parte). */
bool ph_cfg_telefono_app_fissata(const PhCfgTelefono *t, const char *app);
/* Fissa in coda. Gia' presente: 0 e nessun cambio. -1 se il nome non e' valido
 * (NULL, vuoto, troppo lungo) o l'elenco e' pieno (16); il profilo non cambia. */
int ph_cfg_telefono_fissa_app(PhCfgTelefono *t, const char *app, PhErrore *e);
/* Toglie l'app mantenendo l'ordine delle altre. 1 tolta, 0 non c'era. */
int ph_cfg_telefono_togli_app(PhCfgTelefono *t, const char *app);

/* La struttura e' grande (decine di KB): meglio nello heap. */
PhCfgTelefono *ph_cfg_telefono_nuovo(void); /* con i valori predefiniti; NULL se memoria finita */
void ph_cfg_telefono_libera(PhCfgTelefono *t);
void ph_cfg_telefono_predefinito(PhCfgTelefono *t);
int ph_cfg_telefono_da_testo(const char *testo, size_t lunghezza, PhCfgTelefono *t, PhErrore *e);
int ph_cfg_telefono_in_testo(const PhCfgTelefono *t, PhBuffer *out);
int ph_cfg_telefono_carica(const char *seriale, PhCfgTelefono *t, PhErrore *e);
int ph_cfg_telefono_salva(const char *seriale, const PhCfgTelefono *t, PhErrore *e);

#endif
