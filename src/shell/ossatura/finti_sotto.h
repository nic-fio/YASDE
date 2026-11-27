/* Stato condiviso tra i pezzi sotto finti (finti_sotto.c) e la prova dei
 * servizi veri (prova_servizi_veri.c). Solo per la prova. */
#ifndef PH_OSSATURA_FINTI_SOTTO_H
#define PH_OSSATURA_FINTI_SOTTO_H

#include <stdatomic.h>

#include "collegamento/collegamento.h"
#include "dati/dati.h"
#include "meteo/meteo.h"

#define FINTI_RICERCHE_MAX 4

typedef struct {
    PhColOpzioni opz;            /* le richiamate che il collegamento vero chiamerebbe */
    atomic_bool connesso;        /* c'e' una sessione buona */
    atomic_int presti, presti_totali, riconnetti;
    atomic_int ricerche_avviate, ricerche_annullate;
    atomic_int appunti_aperti, appunti_chiusi, ascolti_tolti;
    atomic_int video_aperti, video_chiusi;
    atomic_int apri_dens, ridim_dens, ridim_n; /* ultima densita' di apertura e di ridimensionamento */
    atomic_bool ridim_rifiuta;   /* ph_video_ridimensiona fallisce */
    /* Widget del 23 nov pomeriggio */
    atomic_int cal_modo, sveglia_modo, bat_modo;     /* 0 riuscito, 1 errore, (sveglia) 2 nessuna */
    atomic_int cal_chiamate, cal_giorni_ultimo;      /* ph_dati_calendario_intervallo */
    atomic_bool ctl_rifiuta;                         /* i comandi dei controlli falliscono */
    atomic_int ctl_ascolti, ctl_ascolti_tolti;       /* registrazioni e rimozioni dell'ascolto 0x8e */
    atomic_int ctl_letture;                          /* ph_dati_controlli */
    atomic_int suoneria_n, suoneria_valore, nd_n, torcia_n;
    atomic_int trova_avvia_n, trova_ferma_n, trova_secondi;
    atomic_bool ctl_gate;                            /* ph_dati_controlli aspetta il cancello */
    atomic_bool ctl_in_lettura;                      /* ph_dati_controlli e' fermo al cancello */
    PhDatiAvvisoControlli ctl_cb;                    /* l'ascolto registrato (NULL = nessuno) */
    void *ctl_utente;
    atomic_int pz_n, pz_annulla_n;               /* ph_input_pizzico: tutte e solo ANNULLA */
    atomic_int pz_w, pz_h, pz_x, pz_y, pz_passo; /* valori dell'ultimo passo */
    atomic_int tocco_tentati, tocco_az;   /* ph_input_tocco: entrati (anche se fermi al cancello) e ultima azione */
    atomic_bool input_gate;               /* ph_input_tocco aspetta il cancello dell'input */
    atomic_int abb_n, abb_a_n, abb_porta;   /* ph_col_abbina / ph_col_abbina_a chiamate, porta manuale */
    atomic_bool abb_codice_ok, abb_host_ok; /* argomenti ricevuti uguali a quelli finti dei test */
    atomic_bool abb_rifiuta;                /* il collegamento rifiuta la richiesta */
    atomic_int tocchi_annulla;   /* ph_input_tocco con azione ANNULLA */
    atomic_int tocchi;           /* ph_input_tocco chiamate */
    atomic_int pulisci;          /* ph_inst_pulisci */
    atomic_int pan_on, pan_off;  /* PANNELLO_IMPOSTA 1 e 0 */
    atomic_bool pan_stato_acceso, pan_stato_in_mano; /* cio' che risponde PANNELLO_STATO (default: spento, non in mano) */
    atomic_bool pan_rifiuta;     /* PANNELLO_IMPOSTA fallisce (telefono bloccato o in chiamata) */
    atomic_int app_elenchi;      /* ph_dati_app_elenco chiamate: sta in coda dopo lo spegnimento del collegamento */
    PhEventoGrezzo pan_cb;       /* l'ascolto dell'evento del pannello (0x6e), NULL = nessuno */
    void *pan_utente;
    atomic_int chiavi;           /* ph_video_chiave */
    atomic_int apri_pagina;      /* pagina nella richiesta dell'ultimo ph_video_apri */
    atomic_int pag_n, pag_ultima; /* ph_video_pagina: chiamate e ultima pagina */
    atomic_bool pag_rifiuta;     /* ph_video_pagina fallisce */
    atomic_bool pag_gate, pag_in_corso; /* ph_video_pagina aspetta il cancello / e' ferma li' */
    atomic_int dec_creati, cfg_su_nuovo, esportati;
    atomic_bool video_pacchetti; /* ph_video_leggi manda pacchetti veri */
    atomic_bool dai_fallisce;    /* ph_decodifica_dai rifiuta i fotogrammi */
    atomic_bool dec_senza_hw;    /* la via AUTOMATICA non trova nessuna via hardware: software (caso 1) */
    atomic_bool dec_ripiego;     /* la via hardware rinuncia al primo fotogramma: software (caso 3) */
    atomic_int ordine_n;         /* sequenza di T (tocco), P (pausa), R (ripresa) */
    char ordine[256];
    atomic_int reg_inizia, reg_finisci, reg_libera, reg_pacchetti;
    atomic_int tr_creati, tr_chiusi, tr_staccata, tr_ripresa;
    PhDatiTrovato trovato[FINTI_RICERCHE_MAX];
    PhDatiCercaFine fine[FINTI_RICERCHE_MAX];
    void *utente_ricerca[FINTI_RICERCHE_MAX];
    /* Posizione (ph_dati_posizione) e meteo (meteo.h) */
    atomic_int pos_n, pos_permessa_n;      /* chiamate; con richiesta_permessa vera */
    atomic_bool pos_ultima_permessa;       /* richiesta_permessa dell'ultima chiamata */
    atomic_int pos_esito;                  /* PhDatiPosEsito che il telefono finto risponde */
    atomic_int pos_lat, pos_lon;           /* millesimi di grado, come PhDatiPosizione */
    atomic_bool pos_ricerca, pos_rifiuta;  /* ricerca_avviata; errore (-1) */
    atomic_bool pos_gate, pos_in_corso;    /* la posizione aspetta il cancello / e' ferma li' */
    atomic_int m_creati, m_chiusi, m_chiedi_n, m_annulla_n, m_cerca_n, m_cerca_annulla_n;
    atomic_int m_lat, m_lon;               /* ultime coordinate di ph_meteo_chiedi, in MILLESIMI */
    atomic_bool m_chiedi_rifiuta, m_cerca_rifiuta, m_nuovo_rifiuta;
    atomic_int m_chiedi_dopo_chiudi;       /* chiamate a ph_meteo_* con l'oggetto chiuso (errore) */
    atomic_bool m_chiedi_gate, m_chiedi_in_corso; /* ph_meteo_chiedi aspetta all'ingresso / e' fermo li' */
    atomic_bool altro_seriale;             /* il prossimo prestito e' di un altro telefono (valori finti) */
    char m_cerca_testo[80];                /* solo prova: ultimo testo cercato */
    int adb, comp, dati;         /* oggetti finti: solo gli indirizzi */
} FintiSotto;

extern FintiSotto finti;

/* Apre il cancello di ph_dati_controlli (prova delle corse). */
void finti_apri_cancello(void);
/* Il telefono finto manda l'evento del pannello (0x6e) dal thread «del componente»: acceso o spento,
 * motivo del protocollo 2.9 (1-3 = l'utente o una chiamata). false se nessuno ascolta. */
bool finti_pannello_evento(bool acceso, int motivo);
/* Apre il cancello di ph_video_pagina (con scadenza di 10 s: nessuna attesa senza limite). */
void finti_apri_cancello_pagina(void);
/* Apre il cancello dell'input (canale dell'input "fermo" finche' resta chiuso). */
void finti_apri_cancello_input(void);

/* Posizione che il telefono finto risponde (valori finti). */
void finti_posizione(PhDatiPosEsito esito, int lat_millesimi, int lon_millesimi, const char *localita, bool ricerca_avviata);
void finti_apri_cancello_posizione(void);
/* Il pezzo meteo finto risponde (da un thread suo, come il vero): false se
 * nessuno aspetta (nessuna richiesta, annullata, sostituita o oggetto chiuso). */
bool finti_meteo_rispondi(const PhMeteoPrevisione *p, const char *frase);
bool finti_meteo_cerca_rispondi(const PhMeteoLuogo *v, size_t n, const char *frase);
void finti_apri_cancello_chiedi(void);
/* Consegna la callback della richiesta SOSTITUITA (quella di prima dell'ultima chiedi/cerca): il
 * pezzo vero non lo farebbe, ma una callback gia' in corso al momento della sostituzione si'. */
bool finti_meteo_rispondi_vecchia(const PhMeteoPrevisione *p, const char *frase);
bool finti_meteo_cerca_rispondi_vecchia(const PhMeteoLuogo *v, size_t n, const char *frase);
/* Una previsione finta riconoscibile (temperatura = gradi). */
PhMeteoPrevisione finti_previsione(double gradi);

#endif
