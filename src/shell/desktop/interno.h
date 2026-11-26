/* Parti interne del pezzo shell-desktop (nessun altro pezzo le include). */
#ifndef PH_SHELL_DESKTOP_INTERNO_H
#define PH_SHELL_DESKTOP_INTERNO_H

#include <gtk/gtk.h>
#include <stdbool.h>

#include "config/lingua.h"
#include "logica.h"
/* agente A: inizio */
#include "logica_vecchi.h"
/* agente A: fine */
#include "shell/pezzi.h"

typedef struct PhDesktop PhDesktop;
typedef struct PhDskIcona PhDskIcona;
typedef struct PhDskWidget PhDskWidget;
typedef struct PhDskDialoghi PhDskDialoghi;
typedef struct PhDskLuogo PhDskLuogo;

struct PhDskWidget {
    PhDesktop *d;
    unsigned id;
    size_t indice;          /* posizione in profilo->widget */
    PhCfgWidgetTipo tipo;
    PhCfgWidgetDim dim;     /* livello di contenuto, ricavato dalla misura (ph_dsk_livello) */
    unsigned stile, opzione; /* stile e opzione (citta' del doppio fuso) dal profilo */
    int destra, alto;       /* come si mostra ora (durante il trascinamento) */
    int larghezza, altezza; /* misura libera in px, come si mostra ora */
    PhDskForma forma;
    int percento;           /* riempimento della barra dell'ultima vista (-1 = nessuna barra) */
    int raggio;             /* raggio con cui il vetro e' registrato (segue la misura) */

    GtkWidget *scheda, *interno, *maniglia, *titolo, *testa, *sotto, *barra, *pieno, *quadrante;
    GtkWidget *corpo;       /* contenuto proprio dei widget nuovi (wx_*.c), nascosto se non serve */
    GtkWidget *riga[PH_DSK_RIGHE_MAX], *riga_sx[PH_DSK_RIGHE_MAX], *riga_dx[PH_DSK_RIGHE_MAX];

    /* trascinamento */
    double inizio_x, inizio_y; /* punto di partenza nelle coordinate della radice */
    int destra0, alto0;
    PhDskMisura misura0;       /* misura alla partenza del ridimensionamento */

    void *x;                /* stato proprio dei widget nuovi (wx_*.c), liberato da wx ferma */
};

/* ---- immagine.c: immagini che arrivano dal telefono (dato ostile) ------------
 * Si chiamano da un thread di lavoro: non toccano widget ne' servizi. */

/* Legge un file della cache, controlla la misura sulle intestazioni e lo decodifica.
 * NULL se manca, e' troppo grande o non si decodifica. */
GdkTexture *ph_dsk_immagine_da_cache(const char *nome);
/* Dai byte arrivati dal telefono: misura entro i tetti, decodifica, riduzione alla
 * risoluzione utile (W x H pixel fisici, senza ingrandire), PNG nella cache con
 * un nome nuovo diverso da `precedente`. 0 riuscito (nome in nome_out), -1 errore. */
int ph_dsk_immagine_prepara(GBytes *byte, uint32_t W, uint32_t H, const char *precedente,
                            char *nome_out, size_t cap, PhErrore *e);
/* Toglie dalla cache un file di sfondo vecchio (solo "sfondo-*.png"). */
void ph_dsk_immagine_togli(const char *nome);

/* ---- sfondo.c: il widget che disegna lo sfondo -------------------------------- */
GtkWidget *ph_dsk_sfondo_nuovo(void);
/* ref_larghezza: larghezza del desktop vero, per ridurre in proporzione la misura
 * "Centra" nell'anteprima (0 = nessuna riduzione). */
void ph_dsk_sfondo_imposta(GtkWidget *w, const PhCfgSfondo *cfg, GdkTexture *immagine,
                           int ref_larghezza);

/* ---- desktop.c: il nucleo ------------------------------------------------------- */
struct PhDesktop {
    int rif;         /* 1 + operazioni asincrone in corso */
    unsigned salvataggi; /* quante volte si e' chiesto di salvare il profilo (prova a schermo) */
    bool vivo;       /* false dopo la distruzione della radice: nessun widget si tocca piu' */
    PhShell *sh;
    PhServizi *srv;
    GCancellable *annulla; /* richieste ai servizi in volo */

    GtkWidget *sfondo;     /* PhDskSfondo, a tutta area */
    GtkWidget *libero;     /* icone e widget in posizione assoluta */
    GtkWidget *tratteggio; /* riquadro tratteggiato (figlio di libero) */
    GtkWidget *menu;       /* menu contestuale (si ricostruisce a ogni apertura) */
    GtkWidget *menu_scorri, *menu_voci; /* le voci stanno in un'area che scorre */
    PhDskDialoghi *dlg;
    PhDskLuogo *luogo;     /* dialogo «Località…» del meteo */

    GPtrArray *icone;      /* PhDskIcona* */
    GPtrArray *widget;     /* PhDskWidget* */
    unsigned prossimo_id;  /* identificatori dei widget */
    PhDskGriglia griglia;
    int area_l, area_a;    /* misura della radice */

    GdkTexture *immagine;  /* sfondo immagine decodificato */
    char immagine_nome[PH_CFG_LUN_FILE_CACHE]; /* nome nella cache di quello caricato/in corso */
    unsigned immagine_gen; /* per scartare i caricamenti superati */
    unsigned prep_gen;     /* idem per la preparazione di un'immagine scelta */

    guint ev_srv[PH_SRV_EV_NUMERO];
    guint ev_area, ev_nascoste, ev_host, ev_finestre;
    GWeakRef finestra;     /* la finestra host (per riallineare gli orologi quando torna attiva) */
    gulong id_root, id_attiva;
    guint timer_ora;
    int giorno_visto;      /* giorno dell'anno all'ultimo scatto del timer (0 = mai) */
    int64_t t_memoria, t_calendario; /* istanti (monotonici) delle ultime richieste */
    PhSrvStato stato_prec; /* ultimo stato del collegamento visto */
    GCancellable *annulla_memoria, *annulla_dettaglio, *annulla_calendario;
    bool memoria_nota;
    uint64_t memoria_totale, memoria_libera;
    PhSrvMemoriaDettaglio dettaglio;
    bool dettaglio_noto;
    /* Eventi del calendario: solo in memoria, per il widget a schermo (riservatezza). */
    struct {
        char *titolo;
        uint64_t inizio_ms, fine_ms;
        bool tutto_il_giorno;
    } eventi[PH_SRV_CALENDARIO_MAX];
    size_t n_eventi;
    bool eventi_noti;
    /* agente A: inizio */
    char *app_calendario;           /* pacchetto dell'app Calendario del telefono (NULL = non noto) */
    GCancellable *annulla_app_cal;  /* la richiesta che lo scopre */
    /* agente A: fine */
};

PhDesktop *ph_dsk_ref(PhDesktop *d);
void ph_dsk_unref(PhDesktop *d);

PhCfgTelefono *ph_dsk_profilo(PhDesktop *d);
void ph_dsk_salva(PhDesktop *d); /* ph_srv_profilo_salva (differito) */
bool ph_dsk_widget_visibili(PhDesktop *d);
void ph_dsk_avviso(PhDesktop *d, const char *titolo, const char *testo);
/* Posizione (x, y) di un punto di `w` nelle coordinate della radice. */
void ph_dsk_in_radice(PhDesktop *d, GtkWidget *w, double x, double y, double *rx, double *ry);

/* Sfondo: applica il profilo (ricarica l'immagine se serve) e ridisegna. */
void ph_dsk_sfondo_applica(PhDesktop *d);
/* Imposta lo sfondo nel profilo e lo applica; salva. */
void ph_dsk_sfondo_scegli(PhDesktop *d, const PhCfgSfondo *nuovo);
/* Dalla copia di byte arrivata dal telefono: ridimensiona nel thread e imposta
 * l'immagine come sfondo con l'adattamento dato. */
void ph_dsk_sfondo_da_byte(PhDesktop *d, GBytes *byte, PhCfgAdattamento adatt);
/* Pixel fisici dell'area del desktop (per la riduzione). */
void ph_dsk_misura_fisica(PhDesktop *d, uint32_t *W, uint32_t *H);

/* Menu contestuali (x, y: coordinate della radice). */
void ph_dsk_menu_desktop(PhDesktop *d, double x, double y);
void ph_dsk_menu_icona(PhDesktop *d, const char *app, double x, double y);
void ph_dsk_menu_widget(PhDesktop *d, unsigned id, double x, double y);
/* Un tasto "Menu" o Maiusc+F10 su un controllo: apre il suo menu accanto al controllo. */
void ph_dsk_tasto_menu_aggancia(GtkWidget *w, GCallback aperto, gpointer dati);

/* ---- icone.c ---------------------------------------------------------------------- */
void ph_dsk_icone_ricostruisci(PhDesktop *d);
void ph_dsk_icone_disponi(PhDesktop *d); /* dopo un cambio di area o dei widget visibili */
void ph_dsk_icone_icona_arrivata(PhDesktop *d, const char *pacchetto);
void ph_dsk_icone_nomi_aggiornati(PhDesktop *d);
bool ph_dsk_icone_ha(PhDesktop *d, const char *app);
void ph_dsk_icone_aggiungi(PhDesktop *d, const char *app); /* nella prima cella libera */
void ph_dsk_icone_rimuovi(PhDesktop *d, const char *app);
/* Aggancia i gestori del trascinamento (DnD) al widget: il desktop e' bersaglio per
 * PH_TIPO_APP_TRASCINATA. */
void ph_dsk_icone_bersaglio_aggancia(PhDesktop *d, GtkWidget *w);
void ph_dsk_icone_libera(PhDesktop *d);
void ph_dsk_icone_libera_elemento(gpointer p); /* per g_ptr_array_new_with_free_func */

/* ---- widget.c ----------------------------------------------------------------------- */
void ph_dsk_widget_ricostruisci(PhDesktop *d);
void ph_dsk_widget_riposiziona(PhDesktop *d);   /* dopo un cambio di area */
void ph_dsk_widget_aggiorna_tutti(PhDesktop *d); /* i dati (telefono, notifiche, ora) */
void ph_dsk_widget_memoria_chiedi(PhDesktop *d); /* solo se c'e' un widget Memoria */
void ph_dsk_widget_calendario_chiedi(PhDesktop *d); /* solo se c'e' un widget Calendario */
void ph_dsk_widget_dati_azzera(PhDesktop *d); /* altro telefono: via i dati dell'altro */
void ph_dsk_widget_aggiungi(PhDesktop *d, PhCfgWidgetTipo tipo);
void ph_dsk_widget_rimuovi(PhDesktop *d, unsigned id);
void ph_dsk_widget_libera(PhDesktop *d);
void ph_dsk_widget_libera_elemento(gpointer p); /* per g_ptr_array_new_with_free_func */

/* Stile e opzione di un widget (campi `stile` e `opzione` del profilo): orologio (6 stili,
 * citta' del doppio fuso), calendario del mese (3 stili). */
bool ph_dsk_widget_stile_di(PhDesktop *d, unsigned id, PhCfgWidgetTipo *tipo, unsigned *stile, unsigned *opzione);
void ph_dsk_widget_stile_imposta(PhDesktop *d, unsigned id, unsigned stile, unsigned opzione);
#define PH_DSK_EV_FINESTRE 1000 /* PH_SHELL_EV_FINESTRE, nello stesso spazio dei PH_SRV_EV_* */
void ph_dsk_widget_evento(PhDesktop *d, int ev);
void ph_dsk_widget_richiedi_nuovi(PhDesktop *d);
/* La finestra host e' tornata visibile: ridisegna e riprogramma il timer. */
void ph_dsk_widget_timer_riavvia(PhDesktop *d);
/* Riprogramma solo il timer (senza ridisegnare). */
void ph_dsk_widget_timer_riprogramma(PhDesktop *d);
/* Congela l'ora (secondi dalla mezzanotte, -1 = ora vera): per le prove e le foto del demo. */
void ph_dsk_orologio_ora_fissa(int secondi_dalla_mezzanotte);

/* orologio.c: il quadrante disegnato (classico, essenziale, palette). */
typedef struct {
    unsigned stile;
    int dim;          /* PhCfgWidgetDim */
    int ore, minuti;
    double secondi;   /* solo dove c'e' la lancetta dei secondi */
    char data[96];
} PhDskOrologioDati;
GtkWidget *ph_dsk_orologio_nuovo(void);
void ph_dsk_orologio_imposta(GtkWidget *w, const PhDskOrologioDati *dati);

/* ---- wx_*.c: widget nuovi (CONTRATTI.md §16) ------------------------------------------------
 * Ogni tipo ha una tabella di funzioni. Le richieste ai servizi partono solo se il widget e'
 * mappato, i widget non sono nascosti e il telefono e' collegato; ogni richiesta ha il suo
 * GCancellable e `ferma` le annulla (con i servizi ancora vivi: nel "destroy" della radice). */
typedef struct {
    void (*inizia)(PhDskWidget *w);                       /* dopo la costruzione della scheda */
    void (*ferma)(PhDskWidget *w);                        /* annulla, smette di osservare, libera w->x */
    void (*aggiorna)(PhDskWidget *w, PhDskVista *v);      /* riempie la vista (azzerata) e/o w->corpo */
    void (*richiedi)(PhDskWidget *w);                     /* chiede i dati (le condizioni le controlla wx) */
    void (*tick)(PhDskWidget *w, int64_t ora_us);         /* richieste periodiche, conti alla rovescia */
    bool (*secondi)(PhDskWidget *w);                      /* serve un aggiornamento al secondo */
    void (*evento)(PhDskWidget *w, int ev);               /* avviso dei servizi (PhSrvEvento) o PH_DSK_EV_FINESTRE */
} PhDskWxVt;
const PhDskWxVt *ph_dsk_wx_vt(PhCfgWidgetTipo tipo); /* NULL per i tipi vecchi */
void ph_dsk_wx_inizia(PhDskWidget *w);
void ph_dsk_wx_ferma(PhDskWidget *w);
void ph_dsk_wx_aggiorna(PhDskWidget *w, PhDskVista *v);
void ph_dsk_wx_richiedi(PhDskWidget *w); /* con le condizioni sotto */
void ph_dsk_wx_tick(PhDskWidget *w, int64_t ora_us);
bool ph_dsk_wx_secondi(PhDskWidget *w);
void ph_dsk_wx_evento(PhDskWidget *w, int ev);
/* Condizioni per chiedere al telefono: widget mappato, widget non nascosti, telefono collegato. */
bool ph_dsk_wx_puo_chiedere(PhDskWidget *w);
/* Annulla l'eventuale richiesta in volo e ne prepara una nuova. */
GCancellable *ph_dsk_wx_rinnova(GCancellable **c);
void ph_dsk_wx_annulla(GCancellable **c);
/* Svuota un contenitore (i figli si staccano). */
void ph_dsk_wx_svuota(GtkWidget *box);
/* Le tabelle dei tipi. */
extern const PhDskWxVt PH_DSK_WX_METEO, PH_DSK_WX_CALENDARIO_MESE, PH_DSK_WX_SVEGLIA, PH_DSK_WX_BATTERIA,
    PH_DSK_WX_INTERRUTTORI, PH_DSK_WX_TROVA, PH_DSK_WX_APP_FREQUENTI, PH_DSK_WX_TRASFERIMENTI;

/* Dialogo «Località…» del widget Meteo (meteo_luogo.c). */
void ph_dsk_luogo_apri(PhDesktop *d);
PhDskLuogo *ph_dsk_luogo_nuovo(PhDesktop *d);
void ph_dsk_luogo_libera(PhDskLuogo *l);
/* Comunica ai servizi la citta' del profilo (o nessuna): all'avvio e a ogni profilo caricato. */
void ph_dsk_luogo_comunica(PhDesktop *d);
void ph_dsk_luogo_area(PhDesktop *d);
/* Apre un indirizzo con il lanciatore di URI di GTK (annullabile). */
void ph_dsk_apri_uri(PhDesktop *d, GCancellable *annulla, const char *uri);

/* meteo_disegno.c: icone delle condizioni, barre dei giorni, curva delle ore (cairo). */
/* testo: colore del testo del widget che disegna (serve al contorno e alla scelta fondo chiaro/scuro). */
void ph_dsk_meteo_disegna_icona(cairo_t *cr, const GdkRGBA *testo, int codice, int giorno, double x, double y,
                                double lato);
GtkWidget *ph_dsk_meteo_icona_nuova(int codice, int giorno, int lato);
GtkWidget *ph_dsk_meteo_barra_nuova(bool c_e, double da, double a);
/* t[i]: temperature delle ore (tutte note), ore[i]: «HH»; n >= 2 per disegnare qualcosa. */
GtkWidget *ph_dsk_meteo_curva_nuova(const double *t, const char (*ore)[4], size_t n, int altezza);

/* ---- componenti.c: componenti di disegno comuni dei widget --------------------------------------
 * Tutti: thread GTK, un solo thread; adattano il disegno alla misura che ricevono (nessuna
 * larghezza fissa; il minimo e il naturale sono solo un suggerimento di GTK: chi li mette puo'
 * dare gtk_widget_set_size_request o lasciarli espandere). Nessun colore scritto in C: il testo e'
 * quello della scheda, gli accenti (PhDskAccento) vengono da desktop.css. Tutto cio' che si passa
 * a una *_imposta viene COPIATO (stringhe e array): chi chiama tiene la propria memoria e puo'
 * liberarla subito. I numeri sono dati del telefono: valori negativi, zero, NaN o troppo grandi
 * (anche finiti come 1e308: si riportano in +-1e6) non rompono nulla (si limitano). Anche i TESTI
 * (titoli, nomi, valori, ore) sono dati ostili: UTF-8 non valido si rende valido (g_utf8_make_valid)
 * prima di mostrarlo, e anche nelle etichette accessibili. Chiamare *_imposta con un widget che non e' del tipo giusto
 * non fa nulla. Nessuna callback tranne quella della riga. Si possono richiamare a ogni
 * aggiornamento dei dati; il ridisegno e' accodato, mai fatto subito. */

/* Anello con la percentuale al centro (scheda A1). Misura: quadrata, prende il lato corto. Va messo
 * in un contenitore con halign/valign o una misura ragionevole (gtk_widget_set_size_request(w, 96,
 * 96)); l'anello e' centrato nello spazio.
 *  percento: 0..100, limitato (negativo -> 0, > 100 -> 100); NaN/infinito = dato non noto: solo il
 *            binario e il testo "--".
 *  testo:    cosa scrivere al centro; NULL = "NN%" (la percentuale arrotondata).
 *  acc:      colore dell'arco. */
GtkWidget *ph_dsk_anello_nuovo(void);
void ph_dsk_anello_imposta(GtkWidget *w, double percento, const char *testo, PhDskAccento acc);

/* Barra a fette colorate con la legenda a due colonne (scheda A2). Il widget restituito e' un
 * contenitore verticale (barra alta 14 px + legenda): espande in orizzontale, in verticale prende
 * quel che serve. La legenda e' fatta di etichette con i puntini: con poca larghezza i nomi si
 * accorciano.
 *            La legenda sta in due colonne; se i nomi non ci stanno per intero passa da sola a una
 *            colonna (si ridecide a ogni cambio di larghezza: l'altezza della legenda quindi puo'
 *            crescere, chi mette il componente non deve fissarla).
 *  f[0..n):  nome e valore_testo gia' formattati e tradotti da chi chiama (la barra non scrive numeri
 *            da se'); valore = misura della fetta (qualunque unita', negativo = 0);
 *  totale:   quanto vale tutta la barra (<= 0 = la somma delle fette). Le fette non vanno mai oltre la
 *            barra; con la somma sotto il totale resta uno spazio vuoto (il «libero»).
 *  n:        al massimo PH_DSK_FETTE_MAX, oltre si ignora; f == NULL o n == 0: barra vuota. */
typedef struct {
    const char *nome;
    const char *valore_testo;
    int64_t valore;
    PhDskAccento acc;
} PhDskFetta;
GtkWidget *ph_dsk_fette_nuove(void);
void ph_dsk_fette_imposta(GtkWidget *w, const PhDskFetta *f, size_t n, int64_t totale);
/* agente A: inizio */
/* Solo per le prove del demo: pacchetto di Impostazioni al posto di com.android.settings (NULL = no). */
void ph_dsk_impostazioni_pacchetto_prova(const char *pacchetto);
/* Mostra o nasconde la legenda (la barra resta). Chiamabile in qualunque momento. */
void ph_dsk_fette_legenda_visibile(GtkWidget *w, bool visibile);
/* Colonne (1 o 2) che la legenda usera' se riceve `larghezza` px: la stessa regola con cui decide da
 * sola (voce piu' larga vera, spazio tra le colonne). Vale dopo ph_dsk_fette_imposta. */
int ph_dsk_fette_legenda_colonne(GtkWidget *w, int larghezza);
/* agente A: fine */

/* Curva con area sfumata, i gradi sopra i punti e le ore sotto (scheda A3). Si espande nei due versi;
 * sotto circa 56 px di altezza non si disegna (ci sono solo le etichette).
 *  t[0..n):  valori (non finiti = media degli altri); n al massimo PH_DSK_CURVA_MAX; n = 0 vuota; n = 1
 *            un punto; valori tutti uguali: linea a meta' altezza.
 *  gradi:    NULL = niente etichette sopra; altrimenti n stringhe gia' formattate ("21°"); ore: idem
 *            per le etichette sotto ("16"). Le etichette si diradano da sole se non ci stanno.
 *  acc:      colore della linea e dell'area. */
GtkWidget *ph_dsk_curva_nuova(void);
void ph_dsk_curva_imposta(GtkWidget *w, const double *t, const char *const *gradi, const char *const *ore,
                          size_t n, PhDskAccento acc);

/* Tratto minima-massima di un giorno (scheda A3): un binario che occupa tutta la larghezza con sopra
 * il tratto da `min` a `max` sulla scala scala_min..scala_max (la stessa per tutti i giorni, la
 * calcola chi chiama), sfumato dall'accento `da` ad `a`. Alto 6-12 px, espande in orizzontale.
 *  noto = false (o valori non finiti): solo il binario. Minima e massima scambiate si raddrizzano. */
GtkWidget *ph_dsk_tratto_nuovo(void);
void ph_dsk_tratto_imposta(GtkWidget *w, bool noto, double min, double max, double scala_min, double scala_max,
                           PhDskAccento da, PhDskAccento a);

/* Numero grande con l'etichetta piccola in maiuscolo sopra (schede A2, A5, A6). Il corpo del numero
 * si adatta a larghezza e altezza (da 12 a 44 px).
 *  etichetta: "" o NULL = nessuna; l'etichetta si scrive IN MAIUSCOLO da se' (passala come testo
 *             normale e gia' tradotta);
 *  numero:    "42" / "07:00" (grande); nota: testo piccolo a destra del numero ("GB liberi"), "" = nessuno.
 *  Testi oltre i buffer interni (64/32/64 byte) si tagliano a un confine di carattere. */
GtkWidget *ph_dsk_numero_nuovo(void);
void ph_dsk_numero_imposta(GtkWidget *w, const char *etichetta, const char *numero, const char *nota);

/* Riga d'elenco (scheda A4): tondo o quadrato colorato con sigla (o icona), titolo, sottotitolo
 * troncato con i puntini, ora a destra. Espande in orizzontale; e' un pulsante (fuoco da
 * tastiera, Invio/Spazio, etichetta accessibile «titolo, sottotitolo, ora»).
 *  clic:     callback chiamata dal thread GTK a ogni clic o attivazione da tastiera, con `dati`; NULL =
 *            riga solo da guardare (non prende il fuoco ne' i clic: il trascinamento del widget passa).
 *            La callback puo' chiamare qualunque funzione del desktop, anche ricostruire i widget
 *            (la riga resta viva fino al suo ritorno), ma non deve tenere puntatori alla riga dopo.
 *            Dopo la distruzione della riga non ne arriva nessuna.
 *  dati:     di chi chiama, mai liberato dalla riga: deve vivere piu' della riga (o fino alla
 *            ricostruzione del widget che la contiene).
 *  imposta:  sigla = NULL o "" -> la prima lettera del titolo maiuscola (ph_dsk_sigla); tondo = true
 *            tondo, false quadrato con gli angoli morbidi; sottotitolo e ora NULL o "" = nascosti.
 *            Con un'icona (ph_dsk_riga_icona) la sigla non si vede.
 *            Se titolo e ora non stanno affiancati (larghezza stretta) l'ora scende sulla riga del
 *            sottotitolo, a destra: il titolo prende tutto lo spazio, l'ora non si perde. */
typedef void (*PhDskRigaClic)(void *dati);
GtkWidget *ph_dsk_riga_nuova(PhDskRigaClic clic, void *dati);
void ph_dsk_riga_imposta(GtkWidget *r, const char *sigla, PhDskAccento acc, bool tondo, const char *titolo,
                         const char *sottotitolo, const char *ora);
/* Mette un widget (per esempio l'icona di un'app) al posto della sigla; NULL la rimette. La riga
 * ne prende il possesso (lo stacca da sola): chi chiama non lo libera. L'icona passata deve essere
 * nuova, senza genitore: un'icona che ha gia' un genitore diverso dalla riga NON e' ammessa.
 * Passare di nuovo la stessa icona gia' messa non fa nulla. */
void ph_dsk_riga_icona(GtkWidget *r, GtkWidget *icona);

/* Icone nuove dei widget (wx.c). */
extern const PhShellIcona PH_DSK_ICONA_PREC, PH_DSK_ICONA_SUCC, PH_DSK_ICONA_SUONA, PH_DSK_ICONA_VIBRA,
    PH_DSK_ICONA_SILENZIOSO, PH_DSK_ICONA_LUNA, PH_DSK_ICONA_TORCIA, PH_DSK_ICONA_TROVA;
/* Riaggiorna il contenuto di un widget (dopo l'arrivo di un dato). */
void ph_dsk_widget_aggiorna_uno(PhDskWidget *w);

/* «Trova il telefono» e' FUORI dal catalogo iniziale (decisione del committente,
 * PIANO.md): sul telefono vero non funziona e le prove hanno toccato
 * il volume della suoneria. Il codice del widget resta nel sorgente (wx_controlli.c) ma non e'
 * raggiungibile: non e' nel catalogo, un widget di quel tipo trovato nel profilo si ignora senza
 * errori e senza cancellarlo dal file, e nessuna chiamata a ph_srv_trova_* ne' osservazione dei
 * controlli parte per suo conto. Per riammetterlo: mettere 1 qui e provare di nuovo. */
#define PH_DSK_TROVA_OFFERTO 0

/* Catalogo: una riga per tipo (aggiungere un tipo = una riga). */
typedef struct {
    PhCfgWidgetTipo tipo;
    const char *nome;        /* PH_T_ */
    const char *descrizione; /* PH_T_ */
} PhDskCatalogoVoce;
extern const PhDskCatalogoVoce PH_DSK_CATALOGO[];
extern const size_t PH_DSK_CATALOGO_N;
const char *ph_dsk_widget_nome(PhCfgWidgetTipo tipo); /* tradotto, NULL se fuori catalogo */

/* ---- dialoghi.c ---------------------------------------------------------------------- */
PhDskDialoghi *ph_dsk_dialoghi_nuovi(PhDesktop *d);
void ph_dsk_dialoghi_libera(PhDskDialoghi *dl);
void ph_dsk_dialogo_sfondo_apri(PhDesktop *d);
void ph_dsk_dialogo_widget_apri(PhDesktop *d);
/* Lo sfondo e' cambiato (profilo riletto): il dialogo si riallinea. */
void ph_dsk_dialoghi_sfondo_cambiato(PhDesktop *d);
void ph_dsk_dialoghi_area(PhDesktop *d);
/* Annulla la lettura del file di un'immagine scelta e non ancora arrivata. */
void ph_dsk_dialoghi_annulla_scelta(PhDesktop *d);

/* ---- agente B (passo 2b) ---- */
/* wx_telefono.c, per la prova d'insieme: fa lo stesso del clic su una batteria o una sveglia e torna il
 * pacchetto aperto (NULL = nulla). `forza` != NULL prende il posto del pacchetto. */
const char *ph_dsk_wx_telefono_clic_prova(PhDskWidget *w, const char *forza);
/* ---- fine agente B ---- */

#endif
