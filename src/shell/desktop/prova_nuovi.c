/* Prove di logica di «Chiamate recenti», «Rete» e «In riproduzione» (logica_nuovi.h), SENZA schermo.
 * Ogni prova fallisce se si toglie il codice che prova: valori e confini scelti perche' una soglia,
 * un testo o un caso mancante li faccia cadere. I nomi sono finti. */
#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config/lingua.h"
#include "logica_nuovi.h"

static int errori = 0;
#define VERIFICA(cond)                                                                       \
    do {                                                                                     \
        if (!(cond)) {                                                                       \
            fprintf(stderr, "%s:%d: fallita: %s\n", __FILE__, __LINE__, #cond);              \
            errori++;                                                                        \
        }                                                                                    \
    } while (0)
#define VERIFICA_STR(a, b)                                                                   \
    do {                                                                                     \
        if (strcmp((a), (b)) != 0) {                                                         \
            fprintf(stderr, "%s:%d: \"%s\" != \"%s\"\n", __FILE__, __LINE__, (a), (b));      \
            errori++;                                                                        \
        }                                                                                    \
    } while (0)

static uint64_t ms_utc(int a, int m, int g, int h, int mi)
{
    GDateTime *dt = g_date_time_new_utc(a, m, g, h, mi, 0);
    uint64_t ms = (uint64_t)g_date_time_to_unix(dt) * 1000u;
    g_date_time_unref(dt);
    return ms;
}

/* Il campo senza NUL in fondo a un buffer della misura esatta: con una lettura oltre il campo
 * AddressSanitizer fa cadere la prova. */
static void prova_copia_campo(void)
{
    char *c = g_malloc(8);
    memset(c, 'a', 8);
    char d[16];
    ph_dsk_copia_campo(d, sizeof d, c, 8);
    VERIFICA(strlen(d) == 8 && d[8] == '\0');
    char piccolo[4];
    ph_dsk_copia_campo(piccolo, sizeof piccolo, c, 8);
    VERIFICA(strcmp(piccolo, "aaa") == 0);
    c[3] = '\0';
    ph_dsk_copia_campo(d, sizeof d, c, 8);
    VERIFICA_STR(d, "aaa");
    ph_dsk_copia_campo(d, sizeof d, NULL, 8);
    VERIFICA_STR(d, "");
    ph_dsk_copia_campo(d, 0, c, 8); /* cap 0: nessuna scrittura */
    g_free(c);
    /* Il numero e' l'ultimo campo della chiamata: la struttura esatta lo mette in fondo al buffer. */
    PhSrvChiamata *ch = g_malloc(sizeof *ch);
    memset(ch, 'x', sizeof *ch); /* tutto 'x', nessun NUL: una lettura non limitata esce dal buffer */
    ch->nome[0] = '\0';
    char t[256];
    ph_dsk_chiamata_titolo(ch, t, sizeof t);
    VERIFICA(strlen(t) == sizeof ch->numero - 1); /* letto solo il campo, chiuso nella copia */
    g_free(ch);
}

/* ---- Chiamate ----------------------------------------------------------------------------------- */
static void prova_chiamate_righe(void)
{
    static PhSrvChiamate c; /* grande: statica */
    memset(&c, 0, sizeof c);
    /* Come le notifiche: 22 di titolo, 44 a riga, 2 di spazio, imbottitura verticale 16 + 16. */
    VERIFICA(ph_dsk_chiamate_righe_max(250, 110) == 1);
    VERIFICA(ph_dsk_chiamate_righe_max(250, 10) == 1); /* mai zero */
    VERIFICA(ph_dsk_chiamate_righe_max(250, 230) == 3);
    VERIFICA(ph_dsk_chiamate_righe_max(250, 296) == 5);
    VERIFICA(ph_dsk_chiamate_righe_max(250, 640) > ph_dsk_chiamate_righe_max(250, 296));
    /* Quante se ne mostrano: il minimo tra i dati e il posto; zero se la lettura non e' ok. */
    c.stato = PH_SRV_LET_OK;
    c.n = 2;
    VERIFICA(ph_dsk_chiamate_da_mostrare(&c, 250, 230) == 2);
    c.n = 10;
    VERIFICA(ph_dsk_chiamate_da_mostrare(&c, 250, 230) == 3);
    c.n = 1000; /* dato ostile: oltre il massimo del contratto */
    VERIFICA(ph_dsk_chiamate_da_mostrare(&c, 250, 4000) <= PH_SRV_CHIAMATE_MAX);
    c.n = 5;
    c.stato = PH_SRV_LET_NEGATO;
    VERIFICA(ph_dsk_chiamate_da_mostrare(&c, 250, 230) == 0);
    c.stato = PH_SRV_LET_NON_DISPONIBILE;
    VERIFICA(ph_dsk_chiamate_da_mostrare(&c, 250, 230) == 0);
    c.stato = PH_SRV_LET_ATTESA;
    VERIFICA(ph_dsk_chiamate_da_mostrare(&c, 250, 230) == 0);
    VERIFICA(ph_dsk_chiamate_da_mostrare(NULL, 250, 230) == 0);
}

/* Con 20 righe il numero mostrato segue l'altezza: le righe mostrate stanno intere (22 di titolo,
 * 44 a riga, 2 di spazio, 32 di imbottitura) e una in piu' non ci starebbe; mai meno di una. */
static void prova_chiamate_altezze(void)
{
    static PhSrvChiamate c;
    memset(&c, 0, sizeof c);
    c.stato = PH_SRV_LET_OK;
    c.n = PH_SRV_CHIAMATE_MAX;
    for (int a = 20; a <= 700; a++) {
        size_t k = ph_dsk_chiamate_da_mostrare(&c, 250, a);
        long utile = (long)a - 32;
        VERIFICA(k >= 1 && k <= PH_SRV_CHIAMATE_MAX);
        if (k > 1)
            VERIFICA(22 + (long)k * 44 + ((long)k - 1) * 2 <= utile); /* l'ultima riga e' intera */
        if (k < PH_SRV_CHIAMATE_MAX)
            VERIFICA(k == 1 || 22 + (long)(k + 1) * 44 + (long)k * 2 > utile); /* e una in piu' non ci sta */
    }
}

static void prova_chiamate_stato(void)
{
    static PhSrvChiamate c;
    memset(&c, 0, sizeof c);
    char t[96];
    c.stato = PH_SRV_LET_OK;
    c.n = 0;
    ph_dsk_chiamate_stato_testo(&c, t, sizeof t);
    VERIFICA_STR(t, "Nessuna chiamata recente");
    c.n = 1;
    ph_dsk_chiamate_stato_testo(&c, t, sizeof t);
    VERIFICA_STR(t, "");
    c.stato = PH_SRV_LET_NEGATO;
    ph_dsk_chiamate_stato_testo(&c, t, sizeof t);
    VERIFICA_STR(t, "Non disponibile su questo telefono");
    c.stato = PH_SRV_LET_NON_DISPONIBILE;
    ph_dsk_chiamate_stato_testo(&c, t, sizeof t);
    VERIFICA_STR(t, "Non disponibile su questo telefono");
    c.stato = PH_SRV_LET_ATTESA;
    ph_dsk_chiamate_stato_testo(&c, t, sizeof t);
    VERIFICA_STR(t, "");
    ph_dsk_chiamate_stato_testo(NULL, t, sizeof t);
    VERIFICA_STR(t, "");
    /* Buffer minuscolo: nessuna scrittura oltre, terminato. */
    char piccolo[4] = "xxx";
    c.stato = PH_SRV_LET_NEGATO;
    ph_dsk_chiamate_stato_testo(&c, piccolo, sizeof piccolo);
    VERIFICA(strlen(piccolo) == 3);
}

static void prova_chiamate_riga(void)
{
    VERIFICA(ph_dsk_chiamata_tipo_valido(PH_SRV_CHIAMATA_ENTRANTE) == PH_SRV_CHIAMATA_ENTRANTE);
    VERIFICA(ph_dsk_chiamata_tipo_valido(PH_SRV_CHIAMATA_RIFIUTATA) == PH_SRV_CHIAMATA_RIFIUTATA);
    VERIFICA(ph_dsk_chiamata_tipo_valido(5) == PH_SRV_CHIAMATA_ALTRO); /* fuori elenco */
    VERIFICA(ph_dsk_chiamata_tipo_valido(255) == PH_SRV_CHIAMATA_ALTRO);
    VERIFICA_STR(ph_dsk_chiamata_tipo_testo(PH_SRV_CHIAMATA_ENTRANTE), "Entrante");
    VERIFICA_STR(ph_dsk_chiamata_tipo_testo(PH_SRV_CHIAMATA_USCENTE), "Uscente");
    VERIFICA_STR(ph_dsk_chiamata_tipo_testo(PH_SRV_CHIAMATA_PERSA), "Persa");
    VERIFICA_STR(ph_dsk_chiamata_tipo_testo(PH_SRV_CHIAMATA_RIFIUTATA), "Rifiutata");
    VERIFICA_STR(ph_dsk_chiamata_tipo_testo(PH_SRV_CHIAMATA_ALTRO), "");
    VERIFICA_STR(ph_dsk_chiamata_tipo_testo(99), "");
    /* Solo la persa e' corallo. */
    VERIFICA(ph_dsk_chiamata_accento(PH_SRV_CHIAMATA_PERSA) == PH_DSK_ACC_CORALLO);
    VERIFICA(ph_dsk_chiamata_accento(PH_SRV_CHIAMATA_ENTRANTE) != PH_DSK_ACC_CORALLO);
    VERIFICA(ph_dsk_chiamata_accento(PH_SRV_CHIAMATA_RIFIUTATA) != PH_DSK_ACC_CORALLO);
    VERIFICA(ph_dsk_chiamata_accento(PH_SRV_CHIAMATA_USCENTE) != PH_DSK_ACC_CORALLO);

    /* Titolo: il nome; senza nome il numero; senza niente «Numero nascosto». */
    PhSrvChiamata ch;
    char t[256];
    memset(&ch, 0, sizeof ch);
    strcpy(ch.nome, "Alex Sample");
    strcpy(ch.numero, "+00 000 000 0001");
    ph_dsk_chiamata_titolo(&ch, t, sizeof t);
    VERIFICA_STR(t, "Alex Sample");
    ch.nome[0] = '\0';
    ph_dsk_chiamata_titolo(&ch, t, sizeof t);
    VERIFICA_STR(t, "+00 000 000 0001");
    ch.numero[0] = '\0';
    ph_dsk_chiamata_titolo(&ch, t, sizeof t);
    VERIFICA_STR(t, "Numero nascosto");
    /* Dato ostile: controlli e UTF-8 rotto non arrivano al testo; un nome fatto di soli controlli
     * vale vuoto. */
    memset(&ch, 0, sizeof ch);
    strcpy(ch.nome, "Al\x1b[31mex\x07 \xff" "S");
    ph_dsk_chiamata_titolo(&ch, t, sizeof t);
    VERIFICA(g_utf8_validate(t, -1, NULL) && strchr(t, '\x1b') == NULL && strchr(t, '\x07') == NULL);
    memset(&ch, 0, sizeof ch);
    strcpy(ch.nome, "\x01\x02\x03");
    strcpy(ch.numero, "+00 000 000 0002");
    ph_dsk_chiamata_titolo(&ch, t, sizeof t);
    VERIFICA_STR(t, "+00 000 000 0002");
    /* Campo senza terminatore (buffer pieno): non si legge oltre. */
    memset(ch.nome, 'a', sizeof ch.nome);
    ph_dsk_chiamata_titolo(&ch, t, sizeof t);
    VERIFICA(strlen(t) < sizeof t);
    char piccolo[5];
    ph_dsk_chiamata_titolo(&ch, piccolo, sizeof piccolo);
    VERIFICA(strlen(piccolo) < sizeof piccolo);
    ph_dsk_chiamata_titolo(NULL, t, sizeof t);
    VERIFICA_STR(t, "");
}

static void prova_chiamate_ora(void)
{
    GTimeZone *utc = g_time_zone_new_utc();
    char o[32];
    uint64_t adesso = ms_utc(2026, 11, 27, 15, 0);
    /* Oggi: l'ora. */
    ph_dsk_chiamata_ora(ms_utc(2026, 11, 27, 9, 5), adesso, utc, o, sizeof o);
    VERIFICA_STR(o, "09:05");
    ph_dsk_chiamata_ora(ms_utc(2026, 11, 27, 0, 0), adesso, utc, o, sizeof o); /* confine: mezzanotte */
    VERIFICA_STR(o, "00:00");
    /* Ieri a un minuto dalla mezzanotte: non e' oggi, quindi il giorno. */
    ph_dsk_chiamata_ora(ms_utc(2026, 11, 26, 23, 59), adesso, utc, o, sizeof o);
    VERIFICA_STR(o, "Gio 26");
    ph_dsk_chiamata_ora(ms_utc(2026, 11, 25, 12, 0), adesso, utc, o, sizeof o);
    VERIFICA_STR(o, "Mer 25");
    /* Il fuso conta: le 23:30 UTC sono gia' il giorno dopo a +02:00. */
    GTimeZone *est = g_time_zone_new_identifier("+02:00");
    if (est != NULL) {
        ph_dsk_chiamata_ora(ms_utc(2026, 11, 26, 23, 30), adesso, est, o, sizeof o);
        VERIFICA_STR(o, "01:30");
        g_time_zone_unref(est);
    }
    /* Ora non nota o assurda: vuoto. */
    ph_dsk_chiamata_ora(0, adesso, utc, o, sizeof o);
    VERIFICA_STR(o, "");
    ph_dsk_chiamata_ora(UINT64_MAX, adesso, utc, o, sizeof o);
    VERIFICA_STR(o, "");
    ph_dsk_chiamata_ora(ms_utc(2026, 11, 27, 9, 5), UINT64_MAX, utc, o, sizeof o);
    VERIFICA_STR(o, "");
    g_time_zone_unref(utc);
}

/* ---- Rete ----------------------------------------------------------------------------------------- */
static void prova_rete(void)
{
    VERIFICA(ph_dsk_livello_valido(-3) == 0);
    VERIFICA(ph_dsk_livello_valido(0) == 0);
    VERIFICA(ph_dsk_livello_valido(4) == 4);
    VERIFICA(ph_dsk_livello_valido(5) == 4);
    VERIFICA(ph_dsk_livello_valido(1000) == 4);
    VERIFICA_STR(ph_dsk_rete_generazione(5), "5G");
    VERIFICA_STR(ph_dsk_rete_generazione(4), "4G");
    VERIFICA_STR(ph_dsk_rete_generazione(3), "3G");
    VERIFICA_STR(ph_dsk_rete_generazione(2), "2G");
    VERIFICA_STR(ph_dsk_rete_generazione(0), "");
    VERIFICA_STR(ph_dsk_rete_generazione(1), "");
    VERIFICA_STR(ph_dsk_rete_generazione(6), "");

    PhSrvReteDettaglio r;
    char tw[256], tm[256], nota[96], st[96];
    int lw, lm;
    memset(&r, 0, sizeof r);
    r.stato = PH_SRV_LET_OK;
    r.wifi = PH_SRV_WIFI_CONNESSO;
    r.wifi_livello = 3;
    strcpy(r.wifi_nome, "SampleNet");
    r.mobile = PH_SRV_MOBILE_IN_SERVIZIO;
    r.mobile_livello = 4;
    r.generazione = 5;
    strcpy(r.operatore, "Sample Mobile");
    r.dati_mobili = PH_SRV_TRI_SI;
    ph_dsk_rete_wifi(&r, &lw, tw, sizeof tw);
    ph_dsk_rete_mobile(&r, &lm, tm, sizeof tm, nota, sizeof nota);
    VERIFICA(lw == 3 && lm == 4);
    VERIFICA_STR(tw, "SampleNet");
    VERIFICA_STR(tm, "Sample Mobile \xc2\xb7 5G");
    VERIFICA_STR(nota, "");
    ph_dsk_rete_stato_testo(&r, st, sizeof st);
    VERIFICA_STR(st, "");
    VERIFICA(!ph_dsk_rete_aereo(&r));
    /* Dati mobili spenti: la nota. */
    r.dati_mobili = PH_SRV_TRI_NO;
    ph_dsk_rete_mobile(&r, &lm, tm, sizeof tm, nota, sizeof nota);
    VERIFICA_STR(nota, "Dati mobili spenti");
    VERIFICA_STR(tm, "Sample Mobile \xc2\xb7 5G");
    /* Dati mobili ignoti (2): nessuna nota, come con i dati accesi. */
    r.dati_mobili = PH_SRV_TRI_IGNOTO;
    ph_dsk_rete_mobile(&r, &lm, tm, sizeof tm, nota, sizeof nota);
    VERIFICA_STR(nota, "");
    VERIFICA_STR(tm, "Sample Mobile \xc2\xb7 5G");
    r.dati_mobili = PH_SRV_TRI_SI;
    /* Senza generazione o senza operatore. */
    r.generazione = 0;
    ph_dsk_rete_mobile(&r, &lm, tm, sizeof tm, nota, sizeof nota);
    VERIFICA_STR(tm, "Sample Mobile");
    r.generazione = 4;
    r.operatore[0] = '\0';
    ph_dsk_rete_mobile(&r, &lm, tm, sizeof tm, nota, sizeof nota);
    VERIFICA_STR(tm, "4G");
    r.generazione = 0;
    ph_dsk_rete_mobile(&r, &lm, tm, sizeof tm, nota, sizeof nota);
    VERIFICA_STR(tm, "Connesso");
    /* Livelli assurdi: portati in 0..4. */
    r.wifi_livello = 200;
    r.mobile_livello = 9;
    ph_dsk_rete_wifi(&r, &lw, tw, sizeof tw);
    ph_dsk_rete_mobile(&r, &lm, tm, sizeof tm, nota, sizeof nota);
    VERIFICA(lw == 4 && lm == 4);
    /* Wi-Fi: connesso senza nome, acceso senza rete, spento. */
    r.wifi_nome[0] = '\0';
    ph_dsk_rete_wifi(&r, &lw, tw, sizeof tw);
    VERIFICA_STR(tw, "Connesso");
    r.wifi = PH_SRV_WIFI_ACCESO;
    r.wifi_livello = 3; /* senza rete il livello non conta */
    ph_dsk_rete_wifi(&r, &lw, tw, sizeof tw);
    VERIFICA_STR(tw, "Non connesso");
    VERIFICA(lw == 0);
    r.wifi = PH_SRV_WIFI_SPENTO;
    ph_dsk_rete_wifi(&r, &lw, tw, sizeof tw);
    VERIFICA_STR(tw, "Spento");
    VERIFICA(lw == 0);
    /* Nome della rete ostile. */
    r.wifi = PH_SRV_WIFI_CONNESSO;
    strcpy(r.wifi_nome, "Rete\x1b[0m\xff" "X");
    ph_dsk_rete_wifi(&r, &lw, tw, sizeof tw);
    VERIFICA(g_utf8_validate(tw, -1, NULL) && strchr(tw, '\x1b') == NULL);
    memset(r.wifi_nome, 'w', sizeof r.wifi_nome); /* senza terminatore */
    ph_dsk_rete_wifi(&r, &lw, tw, sizeof tw);
    VERIFICA(strlen(tw) < sizeof tw);
    /* Mobile: senza servizio, senza SIM; il livello e la nota non contano. */
    r.mobile = PH_SRV_MOBILE_SENZA_SERVIZIO;
    r.mobile_livello = 3;
    r.dati_mobili = PH_SRV_TRI_NO;
    ph_dsk_rete_mobile(&r, &lm, tm, sizeof tm, nota, sizeof nota);
    VERIFICA_STR(tm, "Nessun servizio");
    VERIFICA(lm == 0);
    VERIFICA_STR(nota, "");
    r.mobile = PH_SRV_MOBILE_ASSENTE;
    ph_dsk_rete_mobile(&r, &lm, tm, sizeof tm, nota, sizeof nota);
    VERIFICA_STR(tm, "Nessuna SIM");
    VERIFICA(lm == 0);
    /* Modo aereo: solo a lettura ok. */
    r.aereo = PH_SRV_TRI_SI;
    VERIFICA(ph_dsk_rete_aereo(&r));
    r.aereo = PH_SRV_TRI_IGNOTO; /* ignoto: niente «Modalita' aereo» */
    VERIFICA(!ph_dsk_rete_aereo(&r));
    r.aereo = PH_SRV_TRI_SI;
    r.stato = PH_SRV_LET_NEGATO;
    VERIFICA(!ph_dsk_rete_aereo(&r));
    VERIFICA(!ph_dsk_rete_aereo(NULL));
    /* Non disponibile: il testo, e blocchi vuoti. */
    ph_dsk_rete_stato_testo(&r, st, sizeof st);
    VERIFICA_STR(st, "Non disponibile su questo telefono");
    ph_dsk_rete_wifi(&r, &lw, tw, sizeof tw);
    VERIFICA_STR(tw, "");
    VERIFICA(lw == 0);
    r.stato = PH_SRV_LET_NON_DISPONIBILE;
    ph_dsk_rete_stato_testo(&r, st, sizeof st);
    VERIFICA_STR(st, "Non disponibile su questo telefono");
    r.stato = PH_SRV_LET_ATTESA;
    ph_dsk_rete_stato_testo(&r, st, sizeof st);
    VERIFICA_STR(st, "");
    ph_dsk_rete_stato_testo(NULL, st, sizeof st);
    VERIFICA_STR(st, "");
    ph_dsk_rete_mobile(NULL, &lm, tm, sizeof tm, nota, sizeof nota);
    VERIFICA_STR(tm, "");
    /* Buffer minuscoli e puntatori NULL: nessuna scrittura oltre. */
    r.stato = PH_SRV_LET_OK;
    r.mobile = PH_SRV_MOBILE_IN_SERVIZIO;
    strcpy(r.operatore, "Sample Mobile");
    r.generazione = 5;
    char p1[4], p2[2];
    ph_dsk_rete_mobile(&r, NULL, p1, sizeof p1, p2, sizeof p2);
    VERIFICA(strlen(p1) < sizeof p1 && strlen(p2) < sizeof p2);
    ph_dsk_rete_wifi(&r, NULL, p1, sizeof p1);
    VERIFICA(strlen(p1) < sizeof p1);
}

/* ---- In riproduzione ---------------------------------------------------------------------------------- */
static void prova_media(void)
{
    PhSrvMedia m;
    char t[256], st[96];
    memset(&m, 0, sizeof m);
    m.stato_lettura = PH_SRV_LET_OK;
    m.stato = PH_SRV_MEDIA_RIPRODUZIONE;
    strcpy(m.titolo, "Sample Song");
    strcpy(m.artista, "Sample Artist");
    strcpy(m.pacchetto, "it.finto.musica");
    m.comandi = PH_SRV_MEDIA_PUO_PLAY_PAUSA | PH_SRV_MEDIA_PUO_AVANTI;
    VERIFICA(ph_dsk_media_attivo(&m) && ph_dsk_media_suona(&m));
    m.stato = PH_SRV_MEDIA_PAUSA;
    VERIFICA(ph_dsk_media_attivo(&m) && !ph_dsk_media_suona(&m));
    m.stato = PH_SRV_MEDIA_RIPRODUZIONE;
    ph_dsk_media_stato_testo(&m, st, sizeof st);
    VERIFICA_STR(st, "");
    ph_dsk_media_titolo(&m, t, sizeof t);
    VERIFICA_STR(t, "Sample Song");
    ph_dsk_media_artista(&m, t, sizeof t);
    VERIFICA_STR(t, "Sample Artist");
    VERIFICA_STR(ph_dsk_media_pacchetto(&m), "it.finto.musica");
    /* Pulsanti: attivi solo se il bit lo permette. */
    VERIFICA(ph_dsk_media_puo(&m, PH_SRV_MEDIA_CMD_PLAY_PAUSA));
    VERIFICA(ph_dsk_media_puo(&m, PH_SRV_MEDIA_CMD_AVANTI));
    VERIFICA(!ph_dsk_media_puo(&m, PH_SRV_MEDIA_CMD_INDIETRO));
    VERIFICA(!ph_dsk_media_puo(&m, 0));
    VERIFICA(!ph_dsk_media_puo(&m, 4)); /* comando fuori elenco */
    VERIFICA(!ph_dsk_media_puo(&m, -1));
    m.comandi = PH_SRV_MEDIA_PUO_INDIETRO;
    VERIFICA(ph_dsk_media_puo(&m, PH_SRV_MEDIA_CMD_INDIETRO) && !ph_dsk_media_puo(&m, PH_SRV_MEDIA_CMD_AVANTI));
    /* Senza titolo e senza artista. */
    m.titolo[0] = '\0';
    m.artista[0] = '\0';
    ph_dsk_media_titolo(&m, t, sizeof t);
    VERIFICA_STR(t, "Senza titolo");
    ph_dsk_media_artista(&m, t, sizeof t);
    VERIFICA_STR(t, "");
    /* Dato ostile: controlli, UTF-8 rotto, campo senza terminatore, pacchetto non valido. */
    strcpy(m.titolo, "So\x1b[1mng\xff");
    ph_dsk_media_titolo(&m, t, sizeof t);
    VERIFICA(g_utf8_validate(t, -1, NULL) && strchr(t, '\x1b') == NULL);
    memset(m.titolo, 't', sizeof m.titolo);
    memset(m.artista, 'a', sizeof m.artista);
    ph_dsk_media_titolo(&m, t, sizeof t);
    VERIFICA(strlen(t) < sizeof t);
    ph_dsk_media_artista(&m, t, sizeof t);
    VERIFICA(strlen(t) < sizeof t);
    memset(m.pacchetto, 'p', sizeof m.pacchetto);
    VERIFICA(ph_dsk_media_pacchetto(&m) == NULL);
    strcpy(m.pacchetto, "pk\x01g");
    VERIFICA(ph_dsk_media_pacchetto(&m) == NULL);
    m.pacchetto[0] = '\0';
    VERIFICA(ph_dsk_media_pacchetto(&m) == NULL);
    /* Nessuna sessione: «Niente in riproduzione», pulsanti spenti. */
    strcpy(m.pacchetto, "it.finto.musica");
    m.stato = PH_SRV_MEDIA_NESSUNA;
    m.comandi = 7;
    VERIFICA(!ph_dsk_media_attivo(&m) && !ph_dsk_media_suona(&m));
    VERIFICA(!ph_dsk_media_puo(&m, PH_SRV_MEDIA_CMD_PLAY_PAUSA));
    VERIFICA(ph_dsk_media_pacchetto(&m) == NULL);
    ph_dsk_media_stato_testo(&m, st, sizeof st);
    VERIFICA_STR(st, "Niente in riproduzione");
    /* Non disponibile / negato / in attesa. */
    m.stato = PH_SRV_MEDIA_RIPRODUZIONE;
    m.stato_lettura = PH_SRV_LET_NEGATO;
    VERIFICA(!ph_dsk_media_attivo(&m) && !ph_dsk_media_puo(&m, PH_SRV_MEDIA_CMD_AVANTI));
    ph_dsk_media_stato_testo(&m, st, sizeof st);
    VERIFICA_STR(st, "Non disponibile su questo telefono");
    m.stato_lettura = PH_SRV_LET_NON_DISPONIBILE;
    ph_dsk_media_stato_testo(&m, st, sizeof st);
    VERIFICA_STR(st, "Non disponibile su questo telefono");
    m.stato_lettura = PH_SRV_LET_ATTESA;
    ph_dsk_media_stato_testo(&m, st, sizeof st);
    VERIFICA_STR(st, "");
    ph_dsk_media_stato_testo(NULL, st, sizeof st);
    VERIFICA_STR(st, "");
    VERIFICA(!ph_dsk_media_attivo(NULL) && !ph_dsk_media_puo(NULL, 1) && ph_dsk_media_pacchetto(NULL) == NULL);
    ph_dsk_media_titolo(NULL, t, sizeof t);
    VERIFICA_STR(t, "");
}

static void prova_inglese(void)
{
    ph_lingua_imposta(PH_LINGUA_EN);
    char t[96];
    VERIFICA_STR(ph_dsk_chiamata_tipo_testo(PH_SRV_CHIAMATA_PERSA), "Missed");
    VERIFICA_STR(ph_dsk_chiamata_tipo_testo(PH_SRV_CHIAMATA_RIFIUTATA), "Declined");
    PhSrvChiamate *c = g_new0(PhSrvChiamate, 1);
    c->stato = PH_SRV_LET_OK;
    ph_dsk_chiamate_stato_testo(c, t, sizeof t);
    VERIFICA_STR(t, "No recent calls");
    c->stato = PH_SRV_LET_NEGATO;
    ph_dsk_chiamate_stato_testo(c, t, sizeof t);
    VERIFICA_STR(t, "Not available on this phone");
    g_free(c);
    PhSrvReteDettaglio r;
    memset(&r, 0, sizeof r);
    r.stato = PH_SRV_LET_OK;
    char nota[64];
    int l;
    ph_dsk_rete_mobile(&r, &l, t, sizeof t, nota, sizeof nota);
    VERIFICA_STR(t, "No SIM");
    r.mobile = PH_SRV_MOBILE_SENZA_SERVIZIO;
    ph_dsk_rete_mobile(&r, &l, t, sizeof t, nota, sizeof nota);
    VERIFICA_STR(t, "No service");
    ph_dsk_rete_wifi(&r, &l, t, sizeof t);
    VERIFICA_STR(t, "Off");
    r.wifi = PH_SRV_WIFI_ACCESO;
    ph_dsk_rete_wifi(&r, &l, t, sizeof t);
    VERIFICA_STR(t, "Not connected");
    r.mobile = PH_SRV_MOBILE_IN_SERVIZIO;
    r.dati_mobili = PH_SRV_TRI_NO;
    ph_dsk_rete_mobile(&r, &l, t, sizeof t, nota, sizeof nota);
    VERIFICA_STR(nota, "Mobile data off");
    PhSrvMedia m;
    memset(&m, 0, sizeof m);
    m.stato_lettura = PH_SRV_LET_OK;
    ph_dsk_media_stato_testo(&m, t, sizeof t);
    VERIFICA_STR(t, "Nothing playing");
    ph_dsk_media_titolo(&m, t, sizeof t);
    VERIFICA_STR(t, "Untitled");
    ph_lingua_imposta(PH_LINGUA_IT);
}

int main(void)
{
    ph_lingua_imposta(PH_LINGUA_IT);
    prova_copia_campo();
    prova_chiamate_righe();
    prova_chiamate_altezze();
    prova_chiamate_stato();
    prova_chiamate_riga();
    prova_chiamate_ora();
    prova_rete();
    prova_media();
    prova_inglese();
    if (errori != 0) {
        fprintf(stderr, "prova_nuovi: %d verifiche fallite\n", errori);
        return 1;
    }
    printf("prova_nuovi: ok\n");
    return 0;
}
