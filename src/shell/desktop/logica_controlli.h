/* Logica pura del widget Interruttori rapidi (passo 2b, forma a capsula): disposizione dei tre
 * tasti tondi secondo la misura vera, aspetto di ogni stato, scelta dell'icona. Niente widget:
 * si prova senza schermo (prova_controlli.c). */
#ifndef PH_SHELL_DESKTOP_LOGICA_CONTROLLI_H
#define PH_SHELL_DESKTOP_LOGICA_CONTROLLI_H

#include <stdbool.h>
#include <stddef.h>

#include "logica_wx.h"

/* Misure in px. Il tondo con il testo sotto e' piu' piccolo di quello solo: cosi' i tre stanno in
 * una capsula bassa (D2) e, quando l'altezza lo consente, nome e stato stanno sotto (A7). */
#define PH_DSK_CTL_LATO 40       /* diametro del tondo quando c'e' il testo */
#define PH_DSK_CTL_LATO_SOLO 44  /* diametro del tondo quando ci sono solo i tre tondi */
#define PH_DSK_CTL_RIGA 16       /* altezza di una riga di testo quando non e' nota quella vera */
#define PH_DSK_CTL_GAP 4         /* spazio tra il tondo e il testo */
#define PH_DSK_CTL_COL_MIN 64    /* larghezza minima di una colonna con il testo */
#define PH_DSK_CTL_SPAZIO_MIN 8  /* spazio minimo tra due tondi */
#define PH_DSK_CTL_SPAZIO_MAX 24
#define PH_DSK_CTL_MARGINE_V 8   /* px liberi sopra e sotto il blocco, contati dal bordo della capsula */
#define PH_DSK_CTL_MARGINE_TESTO 2 /* respiro in larghezza: il nome non deve toccare il bordo della colonna */

/* Misure vere del testo, prese dai widget (0 = non nota: valgono i valori di riserva). */
typedef struct {
    int riga;         /* altezza di una riga di testo */
    int nome_largo;   /* il nome piu' largo dei tre, scritto su una riga */
    int parola_larga; /* la parola piu' larga dei nomi (sotto questa il nome non va a capo) */
    int riserva_v;    /* px gia' liberi fuori dall'area, sopra e sotto (imbottitura della capsula) */
} PhDskCtlMisure;

typedef struct {
    bool con_testo;  /* sotto ogni tondo nome e stato */
    int righe_nome;  /* 1 o 2 (0 senza testo): i nomi non si tagliano mai, o vanno a capo o non ci sono */
    int riga;        /* altezza di una riga di testo usata */
    int diametro;    /* il tondo */
    int largo, alto; /* misura di ogni tasto (con il testo: colonna intera) */
    int spazio;      /* tra un tasto e il successivo */
    int x, y;        /* angolo del primo tasto nell'area del contenuto: il blocco e' centrato */
} PhDskCtlDisposizione;

/* larghezza e altezza sono quelle dell'AREA DEL CONTENUTO (la scheda senza imbottitura), cioe'
 * la misura vera con cui il widget si disegna. Il testo c'e' solo se il nome sta intero su una riga
 * o su due (a capo tra le parole), se il blocco tondo + nome + stato sta in altezza e se sopra e sotto
 * restano almeno PH_DSK_CTL_MARGINE_V px dal bordo della capsula; altrimenti tre tondi centrati,
 * mai piu' grandi di cio' che entra. Il blocco e' sempre centrato in verticale. Misure non positive:
 * disposizione vuota (diametro 0). `m` puo' essere NULL. */
void ph_dsk_ctl_disposizione(int larghezza, int altezza, const PhDskCtlMisure *m, PhDskCtlDisposizione *out);

/* Come si presenta un controllo: serve a distinguere gli stati anche nella forma dei soli tondi. */
typedef enum {
    PH_DSK_CTL_SPENTO = 0,      /* disponibile, non attivo */
    PH_DSK_CTL_ATTIVO,          /* colorato con l'accento */
    PH_DSK_CTL_IN_CORSO,        /* comando mandato, stato nuovo non ancora arrivato */
    PH_DSK_CTL_NON_DISPONIBILE, /* inattivo, tratteggiato */
    PH_DSK_CTL_ATTESA_DATI,     /* prima risposta dei servizi: nessuno stato presunto */
    PH_DSK_CTL_BORDO            /* solo il bordo d'accento: suoneria che vibra */
} PhDskCtlAspetto;
/* controllo e valore come in ph_dsk_interruttore. Suoneria: «Suona» = pieno d'accento (attivo),
 * «Vibra» = solo bordo d'accento, «Silenzioso» = spento. Non disturbare e torcia: pieno se accesi. */
PhDskCtlAspetto ph_dsk_ctl_aspetto(bool noto, int controllo, int valore, const PhDskInterruttore *ii);

/* Icona del controllo: la suoneria cambia con i suoi tre stati. */
typedef enum {
    PH_DSK_CTL_ICONA_SUONA = 0,
    PH_DSK_CTL_ICONA_VIBRA,
    PH_DSK_CTL_ICONA_SILENZIOSO,
    PH_DSK_CTL_ICONA_LUNA,
    PH_DSK_CTL_ICONA_TORCIA
} PhDskCtlIcona;
PhDskCtlIcona ph_dsk_ctl_icona(int controllo, int valore);

/* Testo per l'etichetta accessibile e per il suggerimento: «Suoneria: Vibra». Nei tondi soli e'
 * l'unico posto dove lo stato e' scritto. Prima della prima risposta l'etichetta dice «In corso» e
 * il suggerimento e' vuoto. Non disponibile: nell'etichetta «Non disponibile», nel suggerimento la
 * spiegazione lunga. */
void ph_dsk_ctl_testi(bool noto, const PhDskInterruttore *ii, char *etichetta, size_t cap_e, char *suggerimento,
                      size_t cap_s);

#endif
