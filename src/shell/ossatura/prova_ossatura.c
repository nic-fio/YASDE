/* Prove di logica di shell-ossatura (F-01, 4.8, 4.14), SENZA schermo. */
#include <locale.h>
#include <stdio.h>
#include <string.h>

#include "config/lingua.h"
#include "logica.h"
#include "shell/shell.h"

void ph_risorse_ossatura_register_resource(void);

static int errori;
#define CONTROLLA(c)                                                                               \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            fprintf(stderr, "prova_ossatura: fallito: %s (riga %d)\n", #c, __LINE__);              \
            errori++;                                                                              \
        }                                                                                          \
    } while (0)

static void prova_velo(void)
{
    ph_lingua_imposta(PH_LINGUA_IT);
    PhVeloContenuto c;
    CONTROLLA(ph_velo_contenuto(PH_SRV_BLOCCATO, &c));
    CONTROLLA(c.lucchetto && c.testo != NULL && !c.pulsante && !c.mostra_motivo);
    CONTROLLA(strcmp(c.titolo, "Telefono bloccato") == 0);
    CONTROLLA(ph_velo_contenuto(PH_SRV_PERSO, &c));
    CONTROLLA(strcmp(c.titolo, "Connessione persa") == 0 && c.pulsante && c.mostra_motivo && !c.lucchetto);
    CONTROLLA(ph_velo_contenuto(PH_SRV_CERCO, &c));
    CONTROLLA(strcmp(c.titolo, "Cerco il telefono") == 0 && !c.pulsante && c.mostra_motivo);
    CONTROLLA(ph_velo_contenuto(PH_SRV_NON_ABBINATO, &c));
    CONTROLLA(strcmp(c.titolo, "Telefono non abbinato") == 0 && !c.pulsante && c.mostra_motivo);
    CONTROLLA(!ph_velo_contenuto(PH_SRV_COLLEGATO, &c));
    CONTROLLA(!ph_velo_contenuto(PH_SRV_CHIUSO, &c));
    /* In inglese i titoli sono tradotti. */
    ph_lingua_imposta(PH_LINGUA_EN);
    CONTROLLA(ph_velo_contenuto(PH_SRV_PERSO, &c));
    CONTROLLA(strcmp(c.titolo, "Connection lost") == 0);
    ph_lingua_imposta(PH_LINGUA_IT);
}

static void prova_host(void)
{
    CONTROLLA(ph_host_visibile_da_stato(0));
    CONTROLLA(ph_host_visibile_da_stato(GDK_TOPLEVEL_STATE_FULLSCREEN));
    CONTROLLA(ph_host_visibile_da_stato(GDK_TOPLEVEL_STATE_FOCUSED | GDK_TOPLEVEL_STATE_MAXIMIZED));
    CONTROLLA(!ph_host_visibile_da_stato(GDK_TOPLEVEL_STATE_MINIMIZED));
    CONTROLLA(!ph_host_visibile_da_stato(GDK_TOPLEVEL_STATE_SUSPENDED));
    CONTROLLA(!ph_host_visibile_da_stato(GDK_TOPLEVEL_STATE_MINIMIZED | GDK_TOPLEVEL_STATE_FOCUSED));
}

static void prova_mkdir(void)
{
    char out[64];
    CONTROLLA(ph_ossatura_comando_mkdir("/sdcard/Nuova", out, sizeof out) == 0);
    CONTROLLA(strcmp(out, "mkdir '/sdcard/Nuova'") == 0);
    /* Un apice nel percorso non esce dagli apici. */
    CONTROLLA(ph_ossatura_comando_mkdir("/sdcard/a'; rm -rf /; '", out, sizeof out) == 0);
    CONTROLLA(strcmp(out, "mkdir '/sdcard/a'\\''; rm -rf /; '\\'''") == 0);
    CONTROLLA(ph_ossatura_comando_mkdir("/a$(x)`y`\n\"z\"", out, sizeof out) == 0);
    CONTROLLA(strcmp(out, "mkdir '/a$(x)`y`\n\"z\"'") == 0);
    /* Vuoto o troppo lungo: errore e buffer sempre terminato. */
    CONTROLLA(ph_ossatura_comando_mkdir("", out, sizeof out) == -1);
    CONTROLLA(ph_ossatura_comando_mkdir(NULL, out, sizeof out) == -1);
    char piccolo[12];
    CONTROLLA(ph_ossatura_comando_mkdir("/sdcard/Nuova", piccolo, sizeof piccolo) == -1);
    CONTROLLA(memchr(piccolo, '\0', sizeof piccolo) != NULL);
    /* Il limite esatto: "mkdir '/x'" sono 10 byte + terminatore. */
    char esatto[11];
    CONTROLLA(ph_ossatura_comando_mkdir("/x", esatto, sizeof esatto) == 0);
    CONTROLLA(strcmp(esatto, "mkdir '/x'") == 0);
    char corto[10];
    CONTROLLA(ph_ossatura_comando_mkdir("/x", corto, sizeof corto) == -1);
    CONTROLLA(ph_ossatura_comando_mkdir("/x", out, 0) == -1);
}

static void prova_macchina(void)
{
    PhFlPasso p;
    /* Prima apertura: APRENDO -> ATTIVO con APERTO. */
    p = ph_fl_passo(PH_FL_APRENDO, PH_FL_EV_APERTO, false);
    CONTROLLA(p.stato == PH_FL_ATTIVO && p.ha_avviso && p.avviso == PH_SRV_FL_APERTO && !p.riapri);
    /* Caduta: ATTIVO -> PERSO con PERSO, una volta sola. */
    p = ph_fl_passo(PH_FL_ATTIVO, PH_FL_EV_SESSIONE_PERSA, true);
    CONTROLLA(p.stato == PH_FL_PERSO && p.ha_avviso && p.avviso == PH_SRV_FL_PERSO);
    p = ph_fl_passo(PH_FL_PERSO, PH_FL_EV_SESSIONE_PERSA, true);
    CONTROLLA(p.stato == PH_FL_PERSO && !p.ha_avviso);
    /* Ritorno: PERSO -> APRENDO con riapertura, senza avviso. */
    p = ph_fl_passo(PH_FL_PERSO, PH_FL_EV_SESSIONE_NUOVA, true);
    CONTROLLA(p.stato == PH_FL_APRENDO && p.riapri && !p.ha_avviso);
    /* Una sessione nuova mentre si sta gia' aprendo o e' attivo: niente. */
    p = ph_fl_passo(PH_FL_APRENDO, PH_FL_EV_SESSIONE_NUOVA, true);
    CONTROLLA(p.stato == PH_FL_APRENDO && !p.riapri && !p.ha_avviso);
    p = ph_fl_passo(PH_FL_ATTIVO, PH_FL_EV_SESSIONE_NUOVA, true);
    CONTROLLA(p.stato == PH_FL_ATTIVO && !p.riapri);
    /* Aperto di nuovo: RIPRESO (non APERTO). */
    p = ph_fl_passo(PH_FL_APRENDO, PH_FL_EV_APERTO, true);
    CONTROLLA(p.stato == PH_FL_ATTIVO && p.avviso == PH_SRV_FL_RIPRESO);
    /* Apertura senza sessione: PERSO e aspetta. */
    p = ph_fl_passo(PH_FL_APRENDO, PH_FL_EV_APERTO_SENZA_SESSIONE, false);
    CONTROLLA(p.stato == PH_FL_PERSO && p.avviso == PH_SRV_FL_PERSO);
    /* Caduta mentre apre: PERSO; il lavoro che finisce dopo non lo riporta in vita. */
    p = ph_fl_passo(PH_FL_APRENDO, PH_FL_EV_SESSIONE_PERSA, false);
    CONTROLLA(p.stato == PH_FL_PERSO);
    p = ph_fl_passo(PH_FL_PERSO, PH_FL_EV_APERTO, false);
    CONTROLLA(p.stato == PH_FL_PERSO && !p.ha_avviso);
    /* Rifiuto del telefono con la connessione viva: ERRORE e non riparte. */
    p = ph_fl_passo(PH_FL_APRENDO, PH_FL_EV_APERTO_FALLITO, false);
    CONTROLLA(p.stato == PH_FL_MORTO && p.avviso == PH_SRV_FL_ERRORE);
    p = ph_fl_passo(PH_FL_MORTO, PH_FL_EV_SESSIONE_NUOVA, true);
    CONTROLLA(p.stato == PH_FL_MORTO && !p.riapri && !p.ha_avviso);
    p = ph_fl_passo(PH_FL_MORTO, PH_FL_EV_SESSIONE_PERSA, true);
    CONTROLLA(p.stato == PH_FL_MORTO && !p.ha_avviso);
    /* Errore del codificatore. */
    p = ph_fl_passo(PH_FL_ATTIVO, PH_FL_EV_CODIFICATORE, true);
    CONTROLLA(p.stato == PH_FL_MORTO && p.avviso == PH_SRV_FL_ERRORE);
    /* Sequenza intera: aperto, due cadute con ritorno, ogni volta RIPRESO. */
    PhFlStato st = PH_FL_APRENDO;
    bool aperto = false;
    int avvisi_ripreso = 0, avvisi_perso = 0, riaperture = 0;
    PhFlEvento seq[] = {PH_FL_EV_APERTO, PH_FL_EV_SESSIONE_PERSA, PH_FL_EV_SESSIONE_NUOVA, PH_FL_EV_APERTO,
                        PH_FL_EV_SESSIONE_PERSA, PH_FL_EV_SESSIONE_NUOVA, PH_FL_EV_APERTO};
    for (size_t i = 0; i < sizeof seq / sizeof seq[0]; i++) {
        p = ph_fl_passo(st, seq[i], aperto);
        st = p.stato;
        if (p.ha_avviso && p.avviso == PH_SRV_FL_RIPRESO)
            avvisi_ripreso++;
        if (p.ha_avviso && p.avviso == PH_SRV_FL_PERSO)
            avvisi_perso++;
        if (p.riapri)
            riaperture++;
        if (p.ha_avviso && (p.avviso == PH_SRV_FL_APERTO || p.avviso == PH_SRV_FL_RIPRESO))
            aperto = true;
    }
    CONTROLLA(st == PH_FL_ATTIVO && avvisi_ripreso == 2 && avvisi_perso == 2 && riaperture == 2);
}

static void prova_pannello(void)
{
    CONTROLLA(ph_ossatura_esito_pannello("chiamata in corso: il pannello resta acceso") == PH_SRV_PAN_CHIAMATA);
    CONTROLLA(ph_ossatura_esito_pannello("telefono bloccato o con lo schermo spento: il pannello non si gestisce") ==
              PH_SRV_PAN_BLOCCATO);
    CONTROLLA(ph_ossatura_esito_pannello("guardiano assente: il pannello non si spegne senza ripristino") ==
              PH_SRV_PAN_RIPRISTINO_INCERTO);
    CONTROLLA(ph_ossatura_esito_pannello("API di Android non disponibile (x): y") == PH_SRV_PAN_NON_DISPONIBILE);
    CONTROLLA(ph_ossatura_esito_pannello("qualcosa d'altro") == PH_SRV_PAN_ERRORE);
    CONTROLLA(ph_ossatura_esito_pannello(NULL) == PH_SRV_PAN_ERRORE);
}

static void prova_vista(void)
{
    PhSrvCollegamento c;
    memset(&c, 0, sizeof c);
    /* Con la schermata della prima connessione (pezzo presente). */
    c.stato = PH_SRV_NON_ABBINATO; /* sempre: serve il codice, anche dopo un collegamento (abbinamento revocato) */
    CONTROLLA(ph_ossatura_vista(&c, false, true) == PH_VISTA_CONNESSIONE);
    CONTROLLA(ph_ossatura_vista(&c, true, true) == PH_VISTA_CONNESSIONE);
    c.stato = PH_SRV_CERCO; /* all'inizio: la schermata; dopo il primo collegamento: il velo */
    CONTROLLA(ph_ossatura_vista(&c, false, true) == PH_VISTA_CONNESSIONE);
    CONTROLLA(ph_ossatura_vista(&c, true, true) == PH_VISTA_VELO);
    c.stato = PH_SRV_PERSO;
    CONTROLLA(ph_ossatura_vista(&c, false, true) == PH_VISTA_CONNESSIONE);
    CONTROLLA(ph_ossatura_vista(&c, true, true) == PH_VISTA_VELO);
    c.stato = PH_SRV_COLLEGATO;
    CONTROLLA(ph_ossatura_vista(&c, true, true) == PH_VISTA_NESSUNA);
    c.schermo_spento = true; /* schermo spento: il velo del blocco, mai la schermata */
    CONTROLLA(ph_ossatura_vista(&c, true, true) == PH_VISTA_VELO);
    c.schermo_spento = false;
    c.stato = PH_SRV_BLOCCATO;
    CONTROLLA(ph_ossatura_vista(&c, true, true) == PH_VISTA_VELO);
    c.stato = PH_SRV_CHIUSO;
    CONTROLLA(ph_ossatura_vista(&c, true, true) == PH_VISTA_NESSUNA);
    /* Senza il pezzo connessione resta il velo di prima (compreso NON_ABBINATO). */
    c.stato = PH_SRV_NON_ABBINATO;
    CONTROLLA(ph_ossatura_vista(&c, false, false) == PH_VISTA_VELO);
    c.stato = PH_SRV_CERCO;
    CONTROLLA(ph_ossatura_vista(&c, false, false) == PH_VISTA_VELO);
}

static void prova_misura(void)
{
    int w, h;
    ph_ossatura_misura_specchio(1080, 2340, &w, &h);
    CONTROLLA(w == 880 && h == 1920);
    ph_ossatura_misura_specchio(1440, 3200, &w, &h);
    CONTROLLA(w == 864 && h == 1920);
    ph_ossatura_misura_specchio(1080, 1920, &w, &h); /* sotto il tetto: invariato */
    CONTROLLA(w == 1080 && h == 1920);
    ph_ossatura_misura_specchio(3000, 1500, &w, &h);
    CONTROLLA(w == 1920 && h == 960);
    ph_ossatura_misura_specchio(0, 0, &w, &h);
    CONTROLLA(w >= 64 && h >= 64 && w <= 1920 && h <= 1920);
    ph_ossatura_misura_specchio(10, 10, &w, &h);
    CONTROLLA(w == 64 && h == 64);
}

/* La scala per GDK_DPI_SCALE ha sempre il punto, anche con la lingua del PC a
 * virgola decimale. Senza una localizzazione a virgola la prova lo dice e salta. */
static void prova_scala_testo(void)
{
    char s[32];
    ph_ossatura_scala_testo(1.25, s, sizeof s);
    CONTROLLA(strcmp(s, "1.25") == 0);
    ph_ossatura_scala_testo(2.0, s, sizeof s);
    CONTROLLA(strcmp(s, "2.00") == 0);
    char piccolo[3];
    ph_ossatura_scala_testo(1.25, piccolo, sizeof piccolo);
    CONTROLLA(strlen(piccolo) == 2);
    const char *nomi[] = {"it_IT.UTF-8", "it_IT.utf8", "de_DE.UTF-8", "fr_FR.UTF-8"};
    bool fatto = false;
    for (size_t i = 0; i < sizeof nomi / sizeof nomi[0] && !fatto; i++) {
        if (setlocale(LC_ALL, nomi[i]) != NULL && localeconv()->decimal_point[0] == ',') {
            ph_ossatura_scala_testo(1.25, s, sizeof s);
            CONTROLLA(strcmp(s, "1.25") == 0);
            fatto = true;
        }
    }
    setlocale(LC_ALL, "C");
    if (!fatto)
        fprintf(stderr, "SALTATA la prova della scala con lingua a virgola: nessuna localizzazione disponibile nel container\n");
}

static void prova_schermo_spento(void)
{
    PhVeloContenuto c;
    PhSrvCollegamento col;
    memset(&col, 0, sizeof col);
    col.stato = PH_SRV_COLLEGATO;
    CONTROLLA(!ph_velo_contenuto_per(&col, &c));
    col.schermo_spento = true; /* schermo spento dal sistema: velo del blocco */
    CONTROLLA(ph_velo_contenuto_per(&col, &c) && c.lucchetto);
    col.stato = PH_SRV_PERSO; /* in PERSO conta lo stato, non lo schermo */
    CONTROLLA(ph_velo_contenuto_per(&col, &c) && c.pulsante);
}

#if PH_OSSATURA_SERVIZI_VERI
static void prova_conversioni(void)
{
    CONTROLLA(ph_ossatura_esito_file(PH_FILE_RIUSCITO) == PH_SRV_ESITO_OK);
    CONTROLLA(ph_ossatura_esito_file(PH_FILE_NON_TROVATO) == PH_SRV_ESITO_NON_TROVATO);
    CONTROLLA(ph_ossatura_esito_file(PH_FILE_NON_CONSENTITO) == PH_SRV_ESITO_NON_CONSENTITO);
    CONTROLLA(ph_ossatura_esito_file(PH_FILE_ERRORE) == PH_SRV_ESITO_ERRORE);
    CONTROLLA(ph_ossatura_esito_file(PH_FILE_ANNULLATO) == PH_SRV_ESITO_ERRORE);
    CONTROLLA(ph_ossatura_esito_file(PH_FILE_ESISTE) == PH_SRV_ESITO_ERRORE);

    CONTROLLA(ph_ossatura_stato_da_collegamento(PH_COL_CERCO) == PH_SRV_CERCO);
    CONTROLLA(ph_ossatura_stato_da_collegamento(PH_COL_COLLEGATO) == PH_SRV_COLLEGATO);
    CONTROLLA(ph_ossatura_stato_da_collegamento(PH_COL_BLOCCATO) == PH_SRV_BLOCCATO);
    CONTROLLA(ph_ossatura_stato_da_collegamento(PH_COL_PERSO) == PH_SRV_PERSO);
    CONTROLLA(ph_ossatura_stato_da_collegamento(PH_COL_CHIUSO) == PH_SRV_CHIUSO);
    CONTROLLA(ph_ossatura_stato_da_collegamento(PH_COL_NON_ABBINATO) == PH_SRV_NON_ABBINATO);

    PhColAvviso a;
    memset(&a, 0, sizeof a);
    a.stato = PH_COL_BLOCCATO;
    a.ha_telefono = true;
    snprintf(a.motivo, sizeof a.motivo, "frase");
    snprintf(a.telefono.seriale, sizeof a.telefono.seriale, "R5CT0000000");
    snprintf(a.telefono.nome, sizeof a.telefono.nome, "Modello finto");
    snprintf(a.telefono.indirizzo, sizeof a.telefono.indirizzo, "192.0.2.1");
    a.telefono.info.api = 34;
    a.telefono.info.larghezza = 1080;
    a.telefono.info.altezza = 2340;
    a.telefono.info.densita = 420;
    PhSrvCollegamento c;
    ph_ossatura_collegamento_da_avviso(&a, &c);
    CONTROLLA(c.stato == PH_SRV_BLOCCATO && c.ha_telefono && c.api == 34 && c.densita == 420);
    CONTROLLA(c.schermo_w == 1080 && c.schermo_h == 2340 && strcmp(c.nome, "Modello finto") == 0);
    CONTROLLA(strcmp(c.motivo, "frase") == 0);
    a.telefoni_trovati = 3;
    a.scelta_arbitraria = true;
    ph_ossatura_collegamento_da_avviso(&a, &c);
    CONTROLLA(c.telefoni_trovati == 3 && c.scelta_arbitraria); /* solo numeri: mai nomi o indirizzi */
    a.stato = PH_COL_PERSO;
    a.ha_telefono = false;
    ph_ossatura_collegamento_da_avviso(&a, &c);
    CONTROLLA(c.stato == PH_SRV_PERSO && !c.ha_telefono && c.nome[0] == '\0' && c.densita == 0);

    PhDatiStato ds = {.batteria = 77, .in_carica = true, .rete = PH_DATI_RETE_CELLULARE, .segnale = 9};
    PhSrvTelefono t = {0};
    t.pannello_acceso = true;
    ph_ossatura_telefono_da_stato(&ds, &t);
    CONTROLLA(t.noto && t.batteria == 77 && t.in_carica && t.rete == PH_SRV_RETE_CELLULARE && t.segnale == 4);
    CONTROLLA(t.pannello_acceso); /* il pannello non lo tocca */

    /* Pannello: evento 0x6e, motivo 1-3 = in mano, 4-5 no, 6 (PC assente) = in mano se acceso. */
    unsigned char ev1[2] = {1, 1}, ev4[2] = {1, 4}, ev0[2] = {0, 1}, ev3[2] = {1, 3};
    unsigned char ev6[2] = {1, 6}, ev6s[2] = {0, 6}, ev7[2] = {1, 7}, ev5[2] = {1, 5};
    CONTROLLA(ph_ossatura_pannello_da_evento(ev6, 2, &t) && t.pannello_acceso && t.in_mano);
    CONTROLLA(ph_ossatura_pannello_da_evento(ev6s, 2, &t) && !t.pannello_acceso && !t.in_mano);
    CONTROLLA(ph_ossatura_pannello_da_evento(ev7, 2, &t) && t.pannello_acceso && !t.in_mano);
    CONTROLLA(ph_ossatura_pannello_da_evento(ev5, 2, &t) && t.pannello_acceso && !t.in_mano);
    CONTROLLA(ph_ossatura_pannello_da_evento(ev1, 2, &t) && t.pannello_noto && t.pannello_acceso && t.in_mano);
    CONTROLLA(ph_ossatura_pannello_da_evento(ev3, 2, &t) && t.in_mano);
    CONTROLLA(ph_ossatura_pannello_da_evento(ev4, 2, &t) && t.pannello_acceso && !t.in_mano);
    CONTROLLA(ph_ossatura_pannello_da_evento(ev0, 2, &t) && !t.pannello_acceso && !t.in_mano);
    CONTROLLA(!ph_ossatura_pannello_da_evento(ev1, 1, &t));
    CONTROLLA(!ph_ossatura_pannello_da_evento(NULL, 0, &t));
    unsigned char st[2] = {1, 0};
    CONTROLLA(ph_ossatura_pannello_da_stato(st, 2, &t) && t.pannello_acceso && !t.in_mano);
    CONTROLLA(!ph_ossatura_pannello_da_stato(st, 1, &t));
}
#endif

int main(void)
{
    ph_risorse_ossatura_register_resource();
    if (ph_shell_stile_controlla("/it/yasde/shell/ossatura.css") != 0) {
        fprintf(stderr, "ossatura.css: errori di sintassi\n");
        errori++;
    }
    prova_velo();
    prova_host();
    prova_mkdir();
    prova_macchina();
    prova_misura();
    prova_pannello();
    prova_vista();
    prova_schermo_spento();
    prova_scala_testo();
#if PH_OSSATURA_SERVIZI_VERI
    prova_conversioni();
#endif
    if (errori == 0)
        printf("prova_ossatura: tutto bene\n");
    return errori == 0 ? 0 : 1;
}
