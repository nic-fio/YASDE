/* Logica pura della superficie (nessun widget, nessun thread GTK): si prova
 * senza schermo (prova_superficie.c). Interno al pezzo shell-superficie. */
#ifndef PH_SUPERFICIE_LOGICA_H
#define PH_SUPERFICIE_LOGICA_H

#include <gdk/gdk.h>
#include <stdatomic.h>
#include <glib.h>
#include <stdbool.h>

#include "shell/fotogramma.h"

#define PH_SUP_MISURA_MAX 8192
#define PH_SUP_PASSO_EXTRA 4096

/* Il fotogramma si puo' usare? Misure 1-8192, per la memoria dati non NULL e
 * passo >= larghezza x byte per pixel e <= quello + PH_SUP_PASSO_EXTRA
 * (e passo x altezza senza overflow: e' la misura che si dichiara a GTK), per il DMA-BUF 1-4 piani con fd >= 0 e
 * passo non nullo. Non rilascia nulla: chi lo riceve decide. */
bool ph_sup_controlla(const PhFotogramma *f);

/* Formato in memoria del fotogramma -> formato di GDK; -1 se sconosciuto. */
int ph_sup_formato_gdk(PhFotoFormato f);
/* Byte per pixel di un formato in memoria; 0 se sconosciuto. */
int ph_sup_byte_per_pixel(PhFotoFormato f);

/* Spazio dei colori -> parametri CICP (H.273). false = PREDEFINITO: non
 * impostare nulla e lasciar decidere a GTK. gamma_ristretta: true = limitata. */
bool ph_sup_cicp(PhFotoColore c, unsigned *primari, unsigned *trasferimento, unsigned *matrice,
                 bool *gamma_ristretta);

/* ---- Scambio del fotogramma in attesa ----------------------------------
 * Tra il thread di lettura e il thread GTK: al massimo UN fotogramma in
 * attesa (il nuovo rilascia quello non ancora mostrato) e al massimo UNA
 * consegna in volo. Ogni fotogramma passato qui viene rilasciato esattamente
 * una volta: o lo riprende chi lo prende, o lo rilascia lo scambio. */
typedef struct {
    GMutex m;
    PhFotogramma attesa;
    bool c_e;      /* c'e' un fotogramma in attesa */
    bool in_volo;  /* una consegna e' partita e non e' ancora arrivata */
    bool chiuso;   /* dopo la chiusura i fotogrammi si rilasciano subito */
} PhSupScambio;

void ph_sup_scambio_inizia(PhSupScambio *s);
/* Libera anche l'eventuale fotogramma in attesa. Nessun altro thread deve
 * piu' usare lo scambio. */
void ph_sup_scambio_libera(PhSupScambio *s);
/* Thread di lettura. *fr passa allo scambio (azzerato). *sostituito = true se
 * ha rilasciato un fotogramma non ancora mostrato. Torna true se chi chiama
 * deve fare la consegna al thread GTK (nessuna in volo). Dopo la chiusura
 * rilascia subito e torna false. */
bool ph_sup_scambio_metti(PhSupScambio *s, PhFotogramma *fr, bool *sostituito);
/* Thread GTK, all'arrivo della consegna: prende il fotogramma in attesa (true)
 * e segna che nessuna consegna e' piu' in volo. */
bool ph_sup_scambio_prendi(PhSupScambio *s, PhFotogramma *out);
/* Da qui in poi niente piu' fotogrammi: rilascia quello in attesa. */
void ph_sup_scambio_chiudi(PhSupScambio *s);

/* ---- Disposizione dell'immagine ----------------------------------------
 * In movimento (trascinamento e ridimensionamento) l'immagine riempie tutta
 * l'area e la scala la GPU; a riposo, se le proporzioni dell'area e del video
 * non coincidono, resta nelle sue proporzioni, centrata, con bande (mai
 * stirata). Misure del video <= 0: tutta l'area. */
void ph_sup_rettangolo(int area_l, int area_a, int video_l, int video_a, bool a_riposo, int *x, int *y,
                       int *l, int *a);


/* ---- Sforzo e rinuncia ai fotogrammi (4.12) ----------------------------
 * Misura dello sforzo del thread GTK, dal lato del thread di lettura: un
 * fotogramma SOSTITUITO nello scambio (arrivato un altro prima che il
 * precedente fosse mostrato) vuol dire che il thread GTK non sta dietro. Se
 * in una finestra di PH_SUP_FINESTRA_US i sostituiti sono almeno
 * PH_SUP_SOGLIA_SOSTITUITI, una superficie NON in primo piano sale di un
 * livello (0 = tutti i fotogrammi, 1 = uno ogni due, 2 = uno ogni quattro; mai
 * oltre); se N finestre pulite di fila (2, raddoppiate a ogni ricaduta fino a
 * 16) si chiudono con zero sostituiti torna a 0 (coda vuota).
 * La superficie in primo piano resta sempre a 0 e non salta mai. Dei fotogrammi
 * saltati se ne tiene da parte UNO (l'ultimo): vedi PhSupRitmo. Mai la
 * risoluzione. Registro: ogni RIDUZIONE di frequenza si scrive sempre; i RITORNI al
 * massimo una riga ogni PH_SUP_LOG_MIN_US; a ogni
 * chiusura di finestra, se il livello attuale differisce dall'ultimo scritto, si
 * scrive lo stato vero (un cambio caduto nel silenzio si recupera). Solo del
 * thread di lettura (nessun lucchetto). */
#define PH_SUP_FINESTRA_US 500000
#define PH_SUP_SOGLIA_SOSTITUITI 2
#define PH_SUP_LIVELLO_MAX 2
#define PH_SUP_LOG_MIN_US 3000000
#define PH_SUP_PULITE_MAX 16 /* tetto delle finestre pulite richieste: 2, 4, 8, 16 dopo ogni ricaduta */

typedef struct {
    gint64 inizio;       /* inizio della finestra corrente (us), 0 = non iniziata */
    unsigned sostituiti; /* sostituiti nella finestra corrente */
    int livello;
    int puliti;          /* finestre di fila senza sostituiti */
    int ricadute;        /* risalite dopo un ritorno a 0: allungano le finestre pulite richieste */
    bool tornato;        /* e' gia' tornata a 0 almeno una volta */
    int livello_scritto; /* ultimo livello scritto nel registro (0 = nessuna riga) */
    gint64 ultimo_log;   /* ultimo cambio riportato (us): niente righe ripetute entro PH_SUP_LOG_MIN_US */
    unsigned contatore;  /* fotogrammi visti, per scegliere quali saltare */
} PhSupSforzo;

/* Registra un fotogramma ricevuto (ora_us monotona) e dice se il livello e'
 * cambiato: *cambiato = true, *causa = sostituiti della finestra che l'ha
 * causato; *per_primo_piano = true se il cambio (ritorno a 0) e' dovuto al
 * passaggio in primo piano e non alla misura. Da chiamare dopo lo scambio, con sostituito. */
void ph_sup_sforzo_nota(PhSupSforzo *s, gint64 ora_us, bool sostituito, bool primo_piano,
                        bool *cambiato, unsigned *causa, bool *per_primo_piano);
/* true = questo fotogramma va rilasciato senza mandarlo allo scambio. */
bool ph_sup_sforzo_salta(PhSupSforzo *s);

/* Rotella: i valori (120esimi di scatto) passano ai servizi cosi' come sono,
 * saturati ai limiti di un i16 del protocollo. */
int ph_sup_satura_i16(int v);

/* ---- Ritmo del thread di lettura ------------------------------------------
 * Sforzo + fotogramma "da parte". Il telefono manda fotogrammi solo quando lo
 * schermo cambia: un fotogramma saltato che resta l'ULTIMO prima di una pausa
 * lascerebbe un'immagine vecchia. Percio' di quelli saltati se ne tiene da parte
 * UNO (l'ultimo, il precedente si rilascia): si rilascia quando ne passa un
 * altro, e va allo scambio se per un po' non arriva nulla (silenzio). Il
 * silenzio e' anche il "battito" che chiude le finestre di misura a tempo,
 * cosi' la riduzione finisce anche quando i fotogrammi si fermano. */
typedef struct {
    PhSupSforzo sf;
    PhFotogramma da_parte;
    bool c_e;
} PhSupRitmo;

/* Fotogramma arrivato. true = va allo scambio (*fr invariato); false = tenuto
 * da parte (*fr azzerato). Se la superficie e' in primo piano non salta mai e
 * azzera subito il livello. */
bool ph_sup_ritmo_arriva(PhSupRitmo *r, PhFotogramma *fr, gint64 ora_us, bool primo_piano, bool *cambiato,
                         unsigned *causa, bool *per_primo_piano);
/* Dopo lo scambio: dice se ha sostituito (sforzo). */
void ph_sup_ritmo_dopo_scambio(PhSupRitmo *r, gint64 ora_us, bool sostituito, bool primo_piano,
                               bool *cambiato, unsigned *causa, bool *per_primo_piano);
/* Nessun fotogramma entro il timeout: chiude la finestra a tempo e, se c'e' un
 * fotogramma da parte, lo da' in *out (true = da mandare allo scambio). */
bool ph_sup_ritmo_silenzio(PhSupRitmo *r, PhFotogramma *out, gint64 ora_us, bool primo_piano,
                           bool *cambiato, unsigned *causa, bool *per_primo_piano);
/* Alla fine del thread: rilascia l'eventuale fotogramma da parte (una volta). */
void ph_sup_ritmo_libera(PhSupRitmo *r);

/* ---- Vita del fotogramma dentro una texture ----------------------------
 * Tiene vivo il fotogramma finche' qualcuno lo usa e lo rilascia UNA volta
 * sola in tutti i casi: memoria (un solo riferimento, quello del GBytes);
 * DMA-BUF con due riferimenti (GTK + chi costruisce): costruzione riuscita,
 * oppure fallita sia che GTK abbia chiamato la funzione di rilascio sia che
 * no (non e' documentato: il flag gtk_chiamato lo dice). */
typedef struct {
    atomic_int rif;
    atomic_bool gtk_chiamato;
    PhFotogramma f;
} PhSupTenuto;

/* Prende il fotogramma (*fr azzerato). riferimenti: 1 memoria, 2 DMA-BUF. */
PhSupTenuto *ph_sup_tenuto_nuovo(PhFotogramma *fr, int riferimenti);
void ph_sup_tenuto_lascia(PhSupTenuto *t);
/* Funzione di rilascio da dare a GTK / GBytes (qualunque thread). */
void ph_sup_tenuto_da_gtk(gpointer p);
/* Fine della costruzione DMA-BUF: riuscita o no, toglie il riferimento di chi
 * costruisce e, se e' fallita senza che GTK abbia chiamato il rilascio, anche
 * quello di GTK. Dopo, t non va piu' usato. */
void ph_sup_tenuto_costruito(PhSupTenuto *t, bool riuscita);

/* ---- Screenshot e schermo protetto ---------------------------------------
 * Decodifica il PNG in memoria in una texture (thread di lavoro: gdk non
 * tocca lo schermo). false = non decodificabile: *frase e' la frase PER
 * L'UTENTE (tradotta, mai il messaggio di GTK, che finisce solo su stderr) e
 * *immagine resta NULL. Le stringhe restituite sono da g_free. */
bool ph_sup_decodifica_png(GBytes *png, GdkTexture **immagine, char **frase);

/* La fine della protezione (era protetto, ora no) chiede UN fotogramma chiave. */
bool ph_sup_chiave_da_chiedere(bool era_protetto, bool ora_protetto);

/* ---- Esito dello screenshot ------------------------------------------------
 * Decisione pura dell'esito che la superficie consegna a chi ha chiesto lo
 * screenshot, dopo la decodifica del PNG per gli appunti.
 *  esito_servizi_ok: lo screenshot dei servizi e' riuscito;
 *  file_salvato: c'e' un percorso;
 *  decodifica: com'e' andata la decodifica (NON_CHIESTA = niente PNG).
 * Uscita: esito_ok (false solo se i servizi hanno fallito, o se la decodifica e'
 * fallita e NON c'e' il file: allora non resta nulla da dare); immagine (presente
 * solo con decodifica riuscita); frase_nostra (la frase tradotta della superficie:
 * quando la decodifica fallisce; altrimenti la frase e' quella dei servizi). */
typedef enum {
    PH_SUP_DEC_NON_CHIESTA = 0,
    PH_SUP_DEC_RIUSCITA,
    PH_SUP_DEC_FALLITA,
    PH_SUP_DEC_NO_THREAD, /* thread di decodifica non creabile: NON si decodifica sul thread GTK */
} PhSupDecodifica;

typedef struct {
    bool esito_ok, immagine, frase_nostra;
} PhSupEsitoScatto;

PhSupEsitoScatto ph_sup_esito_scatto(bool esito_servizi_ok, bool file_salvato, PhSupDecodifica decodifica);

/* ---- Immagine esatta a scala frazionaria ---------------------------------
 * A riposo, con una misura esatta impostata (es_l x es_a > 0, unita' logiche
 * anche non intere: 1000 px fisici a scala 1,5 = 666,667) l'immagine si disegna
 * a partire dall'angolo in alto a sinistra in un rettangolo di quella misura
 * (non allocato: l'allocazione di un widget e' intera). Se la texture non ha le
 * stesse proporzioni di quel rettangolo (cioe' la sua misura in pixel fisici,
 * video x scala, non coincide col rettangolo entro mezzo pixel) resta nelle sue
 * proporzioni, centrata nel rettangolo, con bande: mai stirata. In movimento
 * (a_riposo false) o senza misura esatta vale ph_sup_rettangolo. */
typedef struct {
    double x, y, l, a;
} PhSupRett;

PhSupRett ph_sup_rettangolo_esatto(int area_l, int area_a, double es_l, double es_a, int video_l, int video_a,
                                   double scala, bool a_riposo);


/* ---- Pizzico (Ctrl + rotella, CONTRATTI.md 17) -----------------------------
 * Quando la superficie lo inoltra ai servizi. fase = PH_SRV_PIZZICO_*. Come
 * tocco, rotella e tasti: passa anche con lo schermo protetto (un'app protetta
 * resta usabile anche se non si vede) e con la connessione persa (ci pensano i
 * servizi a scartarlo). Mai in pausa (il telefono non manda nulla e chiude da
 * solo il gesto dopo 400 ms). Fase sconosciuta: scartata. */
bool ph_sup_pizzico_ammesso(int fase, bool in_pausa);

#endif
