/* Il backend FINTO dei servizi (servizi.h): un telefono simulato in memoria,
 * per provare e guardare ogni pezzo della shell senza telefono. Lo usano i
 * demo (demo-<pezzo>) e i test dei pezzi. Di chi coordina: i pezzi non lo
 * modificano (se manca qualcosa lo scrivono nel rapporto).
 *
 * Che cosa simula (dati del mockup Main.dc.html):
 *  - collegamento: CERCO -> COLLEGATO dopo 300 ms; blocco/sblocco e caduta
 *    con ritorno comandabili (sotto, o tasti F5-F8 nei demo);
 *  - 17 app (le 16 del mockup + "Banca" con lo schermo protetto) con icone
 *    generate (quadrato colorato con le iniziali, PNG vero);
 *  - notifiche: le 3 del mockup, poi una nuova ogni notifiche_ogni_s secondi;
 *  - stato: batteria 78% che cala di 1 al minuto, Wi-Fi, pannello spento;
 *  - albero di cartelle del mockup sotto /storage/emulated/0, con /mnt, /data
 *    e /system "non consentite", ricerca, nuova cartella, miniature e lettura
 *    di immagini (generate);
 *  - trasferimenti che avanzano ogni 300 ms (uno alla volta, come la coda
 *    vera); nessun file viene scritto davvero sul PC;
 *  - flussi video sintetici a 60 fotogrammi al secondo alla misura chiesta:
 *    barre colorate, contatore, quadrato che si muove, mirino sull'ultimo
 *    tocco ricevuto; in memoria, o in DMA-BUF (memfd + /dev/udmabuf) con
 *    l'opzione dmabuf, per provare la via senza copie;
 *  - volume, appunti, profilo e preferenze (solo in memoria, mai su disco);
 *  - profilo con le 4 icone del mockup e 2 app fissate sulla barra
 *    (Messaggi, Calcolatrice);
 *  - aggiunte del 23 nov: installazione di APK che avanza in ~2 s (e si puo'
 *    rifiutare), disinstallazione, pannello con esito, memoria per categoria
 *    (app "non leggibile"), calendario con 5 eventi di oggi e uno di tutto il
 *    giorno, prima lettura delle notifiche, schermo spento dal sistema,
 *    screenshot (PNG VERO nella cartella chiesta, mai sovrascritto) e
 *    registrazione SIMULATA (nessun file MP4: FINITA senza percorso);
 *  - widget del 23 nov pomeriggio: calendario su un intervallo (giorni vuoti,
 *    pieni, eventi su piu' giorni, tutto il giorno), prossima sveglia, batteria
 *    in dettaglio (scarica / in carica), interruttori rapidi e «trova il
 *    telefono» (che si ferma da solo dopo il limite); ognuno anche «non
 *    disponibile» o in errore su comando.
 * Ogni comando ricevuto (input, pannello, ...) si scrive su stderr con il
 * prefisso "finto:". */
#ifndef PH_SHELL_FINTO_H
#define PH_SHELL_FINTO_H

#include <gtk/gtk.h>
#include <stdbool.h>

#include "shell/servizi.h"
#include "shell/shell.h"

typedef struct PhFinto PhFinto;

typedef struct {
    int notifiche_ogni_s; /* 0 = 20 s; negativo = mai */
    bool dmabuf;          /* fotogrammi in DMA-BUF se /dev/udmabuf c'e' */
    int ritardo_ms;       /* ritardo simulato delle risposte; 0 = 150 ms */
    int inizio;           /* PhFintoInizio (aggiunto il 23 nov per la prima connessione) */
    bool aspetto_app;     /* fotogrammi con un'immagine neutra "da app" (rettangoli) invece delle barre */
} PhFintoOpzioni;

/* Come parte il telefono finto (prima connessione, src/shell/connessione/):
 *  COLLEGATO     (predefinito) si collega dopo 300 ms, telefono gia' noto;
 *  NON_ABBINATO  un telefono e' in rete ma non riconosce il PC: serve il codice
 *                (codici finti: 123456 = giusto, 000000 = schermata del codice
 *                non aperta sul telefono, altro = codice sbagliato);
 *  CERCA_NOTO    un telefono gia' noto non si trova in rete (resta in CERCO);
 *  CERCA_NUOVO   mai abbinato e nessun telefono in rete (resta in CERCO).
 * Da CERCA_*, ph_finto_telefono_raggiungibile(f, true) fa comparire il
 * telefono: il noto si collega, il nuovo porta a NON_ABBINATO. */
typedef enum {
    PH_FINTO_INIZIO_COLLEGATO = 0,
    PH_FINTO_INIZIO_NON_ABBINATO,
    PH_FINTO_INIZIO_CERCA_NOTO,
    PH_FINTO_INIZIO_CERCA_NUOVO,
} PhFintoInizio;

/* Thread GTK (serve il ciclo principale). opzioni puo' essere NULL. */
PhFinto *ph_finto_nuovo(const PhFintoOpzioni *opzioni);
PhServizi *ph_finto_servizi(PhFinto *f);
/* Chiude i servizi (ph_servizi_libera) e libera. I flussi vanno chiusi prima. */
void ph_finto_libera(PhFinto *f);

/* Comandi (thread GTK). */
void ph_finto_blocca(PhFinto *f, bool bloccato);
void ph_finto_perdi(PhFinto *f, int secondi); /* PERSO per `secondi`, poi torna da solo */
void ph_finto_notifica_nuova(PhFinto *f);
void ph_finto_notifica_aggiorna(PhFinto *f);  /* aggiorna la piu' recente (riappare se nascosta) */
void ph_finto_notifiche_illeggibili(PhFinto *f, bool si);
void ph_finto_batteria(PhFinto *f, int percento);
void ph_finto_chiudi_app_da_sola(PhFinto *f);  /* l'ultima app aperta si chiude da sola */
void ph_finto_appunti_dal_telefono(PhFinto *f, const char *testo);

/* Comandi per le aggiunte del 23 nov (servizi.h, in fondo). Thread GTK. */
/* Esito che il telefono finto da' a ph_srv_pannello_chiedi: PH_SRV_PAN_FATTO
 * (predefinito) = accetta; altrimenti rifiuta con quell'esito. Con il telefono
 * bloccato risponde comunque PH_SRV_PAN_BLOCCATO. */
void ph_finto_pannello_risposta(PhFinto *f, PhSrvPannelloEsito esito);
/* Installazioni: true = il telefono rifiuta (firma diversa). Anche un APK il
 * cui nome contiene "rifiut" viene rifiutato, "errore" da' ERRORE. */
void ph_finto_installazioni_rifiutate(PhFinto *f, bool si);
void ph_finto_calendario_illeggibile(PhFinto *f, bool si);
/* true: dopo ogni collegamento la prima lettura delle notifiche e' VUOTA e le
 * notifiche arrivano 1 s dopo (come nuove). */
void ph_finto_prima_lettura_vuota(PhFinto *f, bool si);
/* Schermo del telefono spento dal sistema (diverso dal blocco): flussi fermi. */
void ph_finto_schermo_spento(PhFinto *f, bool si);
/* true: la prossima registrazione non parte (ERRORE). */
void ph_finto_registrazione_guasta(PhFinto *f, bool si);
/* Quante richieste di fotogramma chiave sono arrivate (per le prove). */
unsigned ph_finto_chiavi_chieste(PhFinto *f);
/* Densita' (dpi) applicata per ultima a un flusso finto (apertura o
 * ridimensionamento); la stessa e' disegnata in giallo nei fotogrammi accanto
 * alla misura. Per le prove. */
int ph_finto_ultima_densita(PhFinto *f);
/* Passi di pizzico accettati dal telefono finto (per le prove). */
unsigned ph_finto_pizzichi(PhFinto *f);
/* Widget del 23 nov pomeriggio. Ogni voce risponde normalmente, «non
 * disponibile» (ERRORE senza frase; per i controlli: nessun interruttore
 * disponibile, valori -1) o ERRORE con una frase (per i controlli: il telefono
 * rifiuta ogni comando). Il calendario del mese risponde ERRORE anche con
 * ph_finto_calendario_illeggibile. */
typedef enum {
    PH_FINTO_VOCE_CALENDARIO_MESE = 0,
    PH_FINTO_VOCE_SVEGLIA,
    PH_FINTO_VOCE_BATTERIA,
    PH_FINTO_VOCE_CONTROLLI,
    PH_FINTO_VOCE_NUMERO
} PhFintoVoce;
typedef enum { PH_FINTO_MODO_NORMALE = 0, PH_FINTO_MODO_NON_DISPONIBILE, PH_FINTO_MODO_ERRORE } PhFintoModo;
void ph_finto_voce_modo(PhFinto *f, PhFintoVoce voce, PhFintoModo modo);
/* Al massimo `massimo` eventi nel calendario del mese (poi «troncato»); 0 = il
 * massimo del contratto. Per provare il ramo «troncato». */
void ph_finto_calendario_massimo(PhFinto *f, int massimo);
/* true: nessuna sveglia impostata (altrimenti domani alle 07:30). */
void ph_finto_sveglia_assente(PhFinto *f, bool si);
/* true: batteria in carica da rete (il livello sale di 1 al minuto; la
 * fotografia del telefono lo dice). */
void ph_finto_in_carica(PhFinto *f, bool si);
/* false: la torcia risulta non disponibile (puo_torcia falso, valore -1). */
void ph_finto_torcia_disponibile(PhFinto *f, bool si);
/* Cambio fatto dal telefono (per esempio qualcuno toglie il silenzioso):
 * aggiorna lo stato e lo pubblica, ma solo mentre i controlli sono osservati. */
void ph_finto_controllo_dal_telefono(PhFinto *f, PhSrvControllo controllo, int valore);
/* Meteo (CONTRATTI.md §18): nessuna rete, nessuna coordinata. Dopo l'osservazione
 * (ph_srv_meteo_osserva) pubblica dopo 300 ms una previsione finta (localita'
 * «Citta' finta»), poi la ripubblica ogni 30 minuti; niente di tutto questo
 * finche' nessuno osserva. Lo stato pubblicato dipende dal modo. */
typedef enum {
    PH_FINTO_METEO_PRONTO = 0,       /* dati freschi */
    PH_FINTO_METEO_IN_ATTESA,        /* la risposta non arriva */
    PH_FINTO_METEO_NON_DISPONIBILE,  /* servizio muto: dati vecchi (e ora) se ce n'erano */
    PH_FINTO_METEO_SENZA_POSIZIONE,  /* posizione del telefono spenta o negata */
    PH_FINTO_METEO_NON_COLLEGATO     /* nessun telefono */
} PhFintoMeteoModo;
#define PH_FINTO_METEO_SCENARI 7
/* Condizioni: 0 poco nuvoloso di giorno (14,5 C, 48 ore e 7 giorni con codici
 * vari e una minima sotto zero), 1 notte serena, 2 pioggia, 3 neve (sotto
 * zero), 4 temporale, 5 nebbia di notte, 6 quasi tutto «non noto» (NAN, -1, 0). */
void ph_finto_meteo_modo(PhFinto *f, PhFintoMeteoModo modo);
void ph_finto_meteo_condizioni(PhFinto *f, int scenario);
/* Arrivano dati nuovi (la temperatura sale di 0,1 C a ogni arrivo). */
void ph_finto_meteo_nuovi_dati(PhFinto *f);
/* true: la prima risposta dopo l'osservazione arriva dopo 10 s invece che 300 ms. */
void ph_finto_meteo_lento(PhFinto *f, bool si);
/* Ricerca dei luoghi (menu «Località…»): senza rete, dopo 300 ms (10 s se lenta)
 * risponde con luoghi INVENTATI che contengono il testo (senza badare alle
 * maiuscole; al massimo 10; «zzz» = nessuno). Con errore: ERRORE «Il servizio
 * meteo non risponde.». Il rifiuto a meteo non osservato lo fa il pezzo comune. */
void ph_finto_meteo_ricerca_errore(PhFinto *f, bool si);
void ph_finto_meteo_ricerca_lenta(PhFinto *f, bool si);
/* Precedenza tra posizione e citta' a mano: la posizione del telefono vince; la citta'
 * (ph_srv_meteo_localita) vale solo con «Posizione spenta» (ph_finto_meteo_modo
 * SENZA_POSIZIONE o NON_COLLEGATO) o a telefono non collegato (ph_finto_perdi): allora
 * PRONTO con localita = nome della citta' e fonte A_MANO. Con la posizione la fonte
 * e' TELEFONO e la localita' «Citta' finta». Al ritorno del telefono la prima
 * risposta riparte (IN_ATTESA con i dati vecchi, poi i nuovi). */
/* Per le prove: quante ricerche di luoghi (ph_srv_meteo_cerca) ancora in corso NON risultano
 * annullate nel momento della chiamata. Chi chiude un dialogo o un widget con una ricerca in
 * volo deve averla annullata: da chiamare dopo ph_shell_libera e prima di ph_finto_libera
 * (deve tornare 0). Le ricerche gia' concluse non contano. Thread GTK. */
int ph_finto_meteo_ricerche_non_annullate(PhFinto *f);
/* Ogni quanti secondi si ripubblica (0 = 30 minuti). Per le prove. */
void ph_finto_meteo_intervallo(PhFinto *f, int secondi);
/* Prima connessione: il telefono compare (o sparisce) in rete. */
void ph_finto_telefono_raggiungibile(PhFinto *f, bool si);

/* Finestrella con i pulsanti dei comandi (per le prove a schermo). */
GtkWidget *ph_finto_comandi_nuovi(PhFinto *f);

/* ---- Demo e prova a schermo ----------------------------------------------
 * Apre una finestra con la shell sopra il backend finto e chiama installa.
 * Se nessun pezzo mette qualcosa nello strato DESKTOP ci mette lo sfondo
 * predefinito (sfumatura 135°, #0e2638 -> #1a625f).
 * Opzioni della riga di comando:
 *   --misura LxA   misura della finestra (predefinita 1280x800)
 *   --comandi      apre anche la finestrella dei comandi
 *   --dmabuf       fotogrammi in DMA-BUF
 *   --stato S      come parte il telefono: collegato (predefinito),
 *                  non-abbinato, cerca-noto, cerca-nuovo (PhFintoInizio)
 *   --prova MS     prova automatica: torna 77 (= saltata per meson) se non
 *                  c'e' uno schermo, altrimenti gira MS millisecondi e torna 0
 * Tasti: F5 blocca/sblocca, F6 connessione persa per 5 s, F7 notifica nuova,
 *        F8 l'ultima app si chiude da sola.
 * Torna il codice d'uscita del programma. */
typedef void (*PhFintoInstalla)(PhShell *sh, PhFinto *f, void *utente);
int ph_finto_demo(int argc, char **argv, const char *titolo, PhFintoInstalla installa, void *utente);

#endif
