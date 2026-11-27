/* Parti interne del backend finto (non e' un contratto). */
#ifndef PH_SHELL_FINTO_INTERNO_H
#define PH_SHELL_FINTO_INTERNO_H

#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>

#include "finto.h"
#include "shell/servizi_backend.h"

typedef struct FlussoFinto FlussoFinto;

struct PhFinto {
    PhServizi *s;
    PhFintoOpzioni o;

    /* Thread di lavoro con il suo contesto: tutto lo stato qui sotto (tranne
     * flussi, protetto da m) si tocca solo da li'. */
    GThread *thread;
    GMainContext *ctx;
    GMainLoop *loop;

    GMutex m;            /* protegge flussi */
    GPtrArray *flussi;   /* di FlussoFinto* */

    PhSrvStato stato;
    bool bloccato;
    guint ritorno;       /* sorgente del ritorno dopo una caduta (0 = nessuna) */
    int batteria;
    bool pannello, in_mano;
    int volume;
    bool muto;
    GPtrArray *notifiche; /* di PhSrvNotifica* */
    int prossima_notifica;
    bool notifiche_illeggibili;
    GHashTable *albero;   /* cartella -> GPtrArray di Voce* */
    GPtrArray *tr;        /* di PhSrvTrasferimento* */
    uint64_t prossimo_tr;
    PhCfgTelefono *profilo_salvato;

    /* Aggiunte del 23 nov (thread di lavoro, tranne chiavi). */
    PhSrvPannelloEsito risposta_pannello;
    bool inst_rifiutate, cal_illeggibile, prima_vuota, schermo_spento, reg_guasta;
    GHashTable *disinstallate; /* pacchetti tolti */
    atomic_uint chiavi;
    /* 27 nov: pagine chieste (per le prove a schermo di finestra-app e desktop). */
    atomic_uint pagine_apertura, pagine_inviate;
    atomic_int ultima_pagina;
    atomic_bool pagina_rifiuta;
    atomic_int ultima_densita; /* densita' applicata per ultima a un flusso (per le prove) */
    /* Prima connessione (thread di lavoro). */
    bool raggiungibile;
    int noti, trovati;
    atomic_uint pizzichi; /* passi di pizzico accettati (per le prove, 23 nov) */
    /* Widget del 23 nov pomeriggio (thread di lavoro). */
    PhFintoModo modo[PH_FINTO_VOCE_NUMERO];
    bool sveglia_assente, in_carica, torcia_manca, ctl_osservati, trova_attivo;
    int suoneria, non_disturbare, torcia;
    /* Meteo (thread di lavoro). */
    PhFintoMeteoModo meteo_modo;
    int meteo_scenario, meteo_seq, meteo_intervallo_s;
    bool meteo_osservato, meteo_lento;
    PhSrvMeteo *meteo_ultimo;               /* ultimi dati buoni (NULL = mai) */
    PhMeteoLuogo *meteo_citta;              /* citta' scelta a mano (NULL = nessuna); mai su disco */
    PhSrvMeteoFonte meteo_ultima_fonte;     /* fonte dei dati in meteo_ultimo */
    GMutex m_ric;                           /* protegge ricerche */
    GPtrArray *ricerche;                    /* PhSrvLavoro* delle ricerche meteo_cerca ancora in corso */
    bool meteo_ricerca_guasta;
    atomic_bool meteo_ricerca_lenta;        /* la ricerca risponde dopo 10 s (scritto dal thread GTK) */
    GSource *meteo_primo, *meteo_ciclo;     /* prima risposta / ripubblicazione periodica */
    int cal_massimo; /* eventi al massimo nel calendario del mese (0 = quello del contratto) */
    GSource *trova_src;  /* arresto automatico di «trova il telefono» (NULL = nessuno) */
    /* Chiamate, rete, media (27 nov; thread di lavoro). Stato in memoria, niente timer: si
     * pubblica all'osservazione e a ogni cambio. */
    bool osserva_chi, osserva_rete, osserva_media;
    bool rete_aereo, media_assente;
    int media_brano, media_stato;       /* indice del brano; PH_SRV_MEDIA_* */
    atomic_int media_ultimo_cmd;        /* ultimo comando ricevuto (0 = nessuno) */
    atomic_uint media_comandi_n;        /* comandi ricevuti */
};

/* Esegue fn(f, dati) sul thread di lavoro dopo ritardo_ms; libera(dati) dopo
 * (anche se il thread si ferma prima: serve a chiudere i lavori). */
void finto_lavora(PhFinto *f, int ritardo_ms, void (*fn)(PhFinto *f, void *dati), void *dati,
                  GDestroyNotify libera);

/* Flussi (flusso_finto.c). */
void *finto_flusso_apri(void *b, PhSrvFlusso *sf, const PhSrvFlussoRichiesta *r, PhErrore *e);
int finto_flusso_leggi(void *b, void *fl, PhFotogramma *out, int timeout_ms);
void finto_flusso_interrompi(void *b, void *fl);
void finto_flusso_memoria(void *b, void *fl);
void finto_flusso_pausa(void *b, void *fl, bool pausa);
void finto_flusso_ridimensiona(void *b, void *fl, int larghezza, int altezza);
void finto_flusso_input(void *b, void *fl, const PhSrvInput *in);
void finto_flusso_chiudi(void *b, void *fl, bool togli_task);
/* Dal thread di lavoro. */
void finto_flussi_perso(PhFinto *f, bool perso);
void finto_flusso_ultimo_si_chiude(PhFinto *f);
/* Aggiunte del 23 nov. */
void finto_flusso_screenshot(void *b, void *fl, PhSrvLavoro *l, unsigned dove, const char *cartella_pc,
                             const char *nome_app);
void finto_flusso_registra(void *b, void *fl, bool avvia, const char *cartella_pc, const char *nome_app,
                           bool con_audio);
void finto_flusso_chiave(void *b, void *fl);
void finto_flusso_pagina(void *b, void *fl, uint8_t pagina);
void finto_flusso_pizzico(void *b, void *fl, int video_w, int video_h, int fase, int x, int y, int passo);
void finto_flusso_ridimensiona_densita(void *b, void *fl, int larghezza, int altezza, int densita);
/* Dal thread di lavoro: flussi fermi per lo schermo spento (nessun avviso). */
void finto_flussi_fermi(PhFinto *f, bool fermi);

/* Immagini generate (PNG): icona con le iniziali, sfumatura. */
GBytes *finto_png_icona(const char *mono, uint32_t colore, int lato);
GBytes *finto_png_sfumatura(uint32_t da, uint32_t a, int larghezza, int altezza);
uint32_t finto_colore_app(const char *pacchetto);

#endif
