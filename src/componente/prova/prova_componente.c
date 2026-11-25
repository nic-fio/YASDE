/* Prova del componente contro il telefono finto (finto.c): avvio, messaggi,
 * video, input, eventi e guasti. Gira sotto AddressSanitizer. */
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "componente/interno.h"
#include "finto.h"

#define VERIFICA(x)                                                                      \
    do {                                                                                 \
        if (!(x)) {                                                                      \
            fprintf(stderr, "%s:%d: verifica fallita: %s\n", __FILE__, __LINE__, #x);    \
            exit(1);                                                                     \
        }                                                                                \
    } while (0)

static int64_t ora_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (int64_t)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

static void pausa_ms(int ms)
{
    struct timespec t = {ms / 1000, (long)(ms % 1000) * 1000000L};
    nanosleep(&t, NULL);
}

/* ---- Banco di prova ------------------------------------------------------*/
typedef struct {
    uint32_t id;
    uint8_t che;
    uint16_t a, b;
} Ev;

typedef struct {
    PhAdb *adb;
    PhComponente *c;
    PhTelefonoInfo info;
    pthread_mutex_t m;
    Ev ev[2048];
    int nev;
} Banco;

static void su_evento(uint32_t id, uint8_t che, uint16_t a, uint16_t b, void *utente)
{
    Banco *bn = utente;
    pthread_mutex_lock(&bn->m);
    if (bn->nev < 2048)
        bn->ev[bn->nev++] = (Ev){id, che, a, b};
    pthread_mutex_unlock(&bn->m);
}

static int numero_eventi(Banco *bn)
{
    pthread_mutex_lock(&bn->m);
    int n = bn->nev;
    pthread_mutex_unlock(&bn->m);
    return n;
}

static bool attendi_eventi(Banco *bn, int n, int ms)
{
    for (int t = 0; t < ms; t += 5) {
        if (numero_eventi(bn) >= n)
            return true;
        pausa_ms(5);
    }
    return numero_eventi(bn) >= n;
}

static void banco_inizia(Banco *bn)
{
    memset(bn, 0, sizeof *bn);
    pthread_mutex_init(&bn->m, NULL);
    bn->adb = finto_crea();
    VERIFICA(bn->adb != NULL);
}

static PhComponente *banco_avvia(Banco *bn, int timeout_ms, PhErrore *e)
{
    bn->c = ph_componente_avvia(bn->adb, "/percorso/finto.jar", su_evento, bn, &bn->info, timeout_ms, e);
    return bn->c;
}

static void banco_fine(Banco *bn)
{
    if (bn->c != NULL)
        ph_componente_chiudi(bn->c);
    bn->c = NULL;
    finto_distruggi(bn->adb);
    pthread_mutex_destroy(&bn->m);
}

static void banco_pronto(Banco *bn)
{
    banco_inizia(bn);
    PhErrore e;
    VERIFICA(banco_avvia(bn, 3000, &e) != NULL);
}

static void comando_uguale(PhAdb *a, size_t i, uint8_t tipo, const uint8_t *corpo, size_t n)
{
    uint8_t t = 0, buf[512];
    VERIFICA(n <= sizeof buf);
    size_t len = finto_comando(a, i, &t, buf, sizeof buf);
    VERIFICA(len == n && t == tipo && (n == 0 || memcmp(buf, corpo, n) == 0));
}

/* Il telefono finto registra la riga di apertura dal suo thread: si aspetta un attimo. */
static bool canali_aperti(PhAdb *a, const char *tipo, int n)
{
    for (int t = 0; t < 2000; t += 5) {
        if (finto_canali_aperti(a, tipo) == n)
            return true;
        pausa_ms(5);
    }
    return false;
}

static PhVideoRichiesta richiesta(const char *pacchetto)
{
    return (PhVideoRichiesta){640, 480, 320, 4000000, 30, pacchetto};
}

/* Legge e controlla tutti i pacchetti normali di una sessione. */
static void leggi_pacchetti_normali(PhVideoFlusso *f, int totale, uint16_t l, uint16_t h)
{
    uint8_t atteso[4096];
    for (int i = -1; i < totale - 1; i++) {
        PhVideoPacchetto p;
        PhErrore e;
        VERIFICA(ph_video_leggi(f, &p, 3000, &e) == 1);
        size_t n = finto_dati_pacchetto(i, atteso, sizeof atteso);
        VERIFICA(p.configurazione == (i < 0));
        VERIFICA(p.chiave == (i == 0));
        VERIFICA(p.larghezza == l && p.altezza == h);
        VERIFICA(p.tempo_us == finto_tempo_pacchetto(i));
        VERIFICA(p.lunghezza == n && memcmp(p.dati, atteso, n) == 0);
    }
}

/* /data/local/tmp/yasde-<8 cifre esadecimali>.jar */
static void forma_jar(const char *p)
{
    const char *pre = "/data/local/tmp/yasde-";
    size_t lp = strlen(pre);
    VERIFICA(strncmp(p, pre, lp) == 0 && strlen(p) == lp + 8 + 4 && strcmp(p + lp + 8, ".jar") == 0);
    for (size_t i = 0; i < 8; i++)
        VERIFICA((p[lp + i] >= '0' && p[lp + i] <= '9') || (p[lp + i] >= 'a' && p[lp + i] <= 'f'));
}

/* ---- Prove ---------------------------------------------------------------*/
static void prova_avvio_e_chiusura(void)
{
    Banco bn;
    banco_pronto(&bn);
    VERIFICA(bn.info.api == 34 && bn.info.larghezza == 1080 && bn.info.altezza == 2340 &&
             bn.info.densita == 420 && strcmp(bn.info.modello, "Telefono finto") == 0);
    VERIFICA(strcmp(finto_jar_locale(bn.adb), "/percorso/finto.jar") == 0);
    forma_jar(finto_jar_remoto(bn.adb));
    char atteso_lancio[300];
    snprintf(atteso_lancio, sizeof atteso_lancio, "CLASSPATH=%s app_process / it.yasde.Servizio",
             finto_jar_remoto(bn.adb));
    VERIFICA(strcmp(finto_servizio_lanciato(bn.adb), atteso_lancio) == 0); /* stesso percorso copiato e lanciato */
    VERIFICA(ph_componente_vivo(bn.c));
    VERIFICA(canali_aperti(bn.adb, "comandi", 1) && canali_aperti(bn.adb, "input", 1));
    comando_uguale(bn.adb, 0, PH_MSG_SALUTO, (const uint8_t[]){0, 1}, 2);
    ph_componente_chiudi(bn.c);
    bn.c = NULL;
    VERIFICA(finto_ricevuto_chiudi(bn.adb));
    banco_fine(&bn);
}

static void prova_nomi_diversi(void)
{
    Banco a, b;
    banco_pronto(&a);
    banco_pronto(&b);
    forma_jar(finto_jar_remoto(a.adb));
    forma_jar(finto_jar_remoto(b.adb));
    VERIFICA(strcmp(finto_jar_remoto(a.adb), finto_jar_remoto(b.adb)) != 0);
    banco_fine(&a);
    banco_fine(&b);
}

/* Dopo un avvio fallito il PC ha tentato di togliere il jar con quel nome. */
static void pulizia_giusta(Banco *bn)
{
    char rm[300];
    forma_jar(finto_jar_remoto(bn->adb));
    snprintf(rm, sizeof rm, "rm -f %s", finto_jar_remoto(bn->adb));
    VERIFICA(strcmp(finto_comando_pulizia(bn->adb), rm) == 0);
}

static void prova_errori_di_avvio(void)
{
    PhErrore e;
    Banco bn;

    banco_inizia(&bn);
    finto_cfg(bn.adb)->copia_fallisce = true;
    VERIFICA(banco_avvia(&bn, 1000, &e) == NULL);
    VERIFICA(strstr(e.testo, "copia") != NULL && strstr(e.testo, "spazio sul telefono esaurito") != NULL);
    pulizia_giusta(&bn);
    banco_fine(&bn);

    banco_inizia(&bn);
    finto_cfg(bn.adb)->niente_pronto = true;
    int64_t t0 = ora_ms();
    VERIFICA(banco_avvia(&bn, 300, &e) == NULL);
    VERIFICA(ora_ms() - t0 < 3000);
    VERIFICA(strstr(e.testo, "PRONTO") != NULL);
    {
        char rm[300];
        snprintf(rm, sizeof rm, "rm -f %s", finto_jar_remoto(bn.adb));
        forma_jar(finto_jar_remoto(bn.adb));
        VERIFICA(strcmp(finto_comando_pulizia(bn.adb), rm) == 0); /* cancella lo stesso file copiato */
    }
    pulizia_giusta(&bn);
    banco_fine(&bn);

    banco_inizia(&bn);
    finto_cfg(bn.adb)->shell_muore = true;
    VERIFICA(banco_avvia(&bn, 2000, &e) == NULL);
    VERIFICA(strstr(e.testo, "classe it.yasde.Servizio non trovata") != NULL);
    pulizia_giusta(&bn);
    banco_fine(&bn);

    banco_inizia(&bn);
    finto_cfg(bn.adb)->saluto_rifiuta = true;
    VERIFICA(banco_avvia(&bn, 2000, &e) == NULL);
    VERIFICA(strstr(e.testo, "versione del protocollo non supportata") != NULL);
    pulizia_giusta(&bn);
    banco_fine(&bn);

    banco_inizia(&bn);
    finto_cfg(bn.adb)->versione = 2;
    VERIFICA(banco_avvia(&bn, 2000, &e) == NULL);
    VERIFICA(strstr(e.testo, "versione 2") != NULL);
    pulizia_giusta(&bn);
    banco_fine(&bn);

    banco_inizia(&bn);
    finto_cfg(bn.adb)->silenzio = true;
    t0 = ora_ms();
    VERIFICA(banco_avvia(&bn, 300, &e) == NULL);
    VERIFICA(ora_ms() - t0 < 3000);
    VERIFICA(strstr(e.testo, "non ha risposto") != NULL);
    pulizia_giusta(&bn);
    banco_fine(&bn);

    /* argomenti mancanti e PhErrore NULL */
    banco_inizia(&bn);
    VERIFICA(ph_componente_avvia(NULL, "x", NULL, NULL, NULL, 100, &e) == NULL);
    VERIFICA(ph_componente_avvia(bn.adb, NULL, NULL, NULL, NULL, 100, NULL) == NULL);
    finto_cfg(bn.adb)->copia_fallisce = true;
    VERIFICA(ph_componente_avvia(bn.adb, "x", NULL, NULL, NULL, 100, NULL) == NULL);
    pulizia_giusta(&bn); /* copia interrotta, anche con PhErrore NULL */
    banco_fine(&bn);
}

static void prova_video(void)
{
    Banco bn;
    banco_pronto(&bn);
    PhErrore e;
    PhVideoRichiesta r = richiesta("com.esempio.app");
    PhVideoFlusso *f1 = ph_video_apri(bn.c, &r, &e);
    VERIFICA(f1 != NULL && ph_video_id(f1) == 1);
    /* VIDEO_APRI: id, 640, 480, 320, 4000000, 30, "com.esempio.app" */
    uint8_t atteso[64] = {0, 0, 0, 1, 0x02, 0x80, 0x01, 0xe0, 0x01, 0x40, 0x00, 0x3d, 0x09, 0x00, 30, 0, 15};
    memcpy(atteso + 17, "com.esempio.app", 15);
    comando_uguale(bn.adb, 1, PH_MSG_VIDEO_APRI, atteso, 32);
    VERIFICA(canali_aperti(bn.adb, "video:1", 1));

    /* seconda sessione, specchio dello schermo (pacchetto NULL) */
    PhVideoRichiesta r2 = richiesta(NULL);
    r2.larghezza = 1080;
    r2.altezza = 2336;
    PhVideoFlusso *f2 = ph_video_apri(bn.c, &r2, &e);
    VERIFICA(f2 != NULL && ph_video_id(f2) == 2);
    uint8_t t;
    uint8_t corpo[64];
    VERIFICA(finto_comando(bn.adb, 2, &t, corpo, sizeof corpo) == 17 && corpo[15] == 0 && corpo[16] == 0);

    leggi_pacchetti_normali(f1, 6, 640, 480);
    leggi_pacchetti_normali(f2, 6, 1080, 2336);
    PhVideoPacchetto p;
    VERIFICA(ph_video_leggi(f1, &p, 2000, &e) == 0);
    VERIFICA(ph_video_leggi(f1, &p, 2000, &e) == 0); /* resta finito */

    ph_video_chiudi(f1, true);
    comando_uguale(bn.adb, 3, PH_MSG_VIDEO_CHIUDI, (const uint8_t[]){0, 0, 0, 1, 1}, 5);
    ph_video_chiudi(f2, false);
    comando_uguale(bn.adb, 4, PH_MSG_VIDEO_CHIUDI, (const uint8_t[]){0, 0, 0, 2, 0}, 5);

    /* misure non valide: rifiutate sul PC senza mandare nulla */
    size_t prima = finto_numero_comandi(bn.adb);
    PhVideoRichiesta cattiva = richiesta("x");
    cattiva.larghezza = 100;
    VERIFICA(ph_video_apri(bn.c, &cattiva, &e) == NULL);
    cattiva.larghezza = 4096;
    VERIFICA(ph_video_apri(bn.c, &cattiva, &e) == NULL);
    cattiva.larghezza = 640;
    cattiva.altezza = 60;
    VERIFICA(ph_video_apri(bn.c, &cattiva, &e) == NULL);
    VERIFICA(finto_numero_comandi(bn.adb) == prima);
    VERIFICA(ph_video_apri(bn.c, NULL, &e) == NULL);
    banco_fine(&bn);
}

static void prova_richieste_ed_errori(void)
{
    Banco bn;
    banco_pronto(&bn);
    PhErrore e;
    PhVideoRichiesta r = richiesta("com.a");
    PhVideoFlusso *f = ph_video_apri(bn.c, &r, &e);
    VERIFICA(f != NULL);
    uint16_t lv = 0, av = 0;
    VERIFICA(ph_video_ridimensiona(f, 800, 600, 240, &lv, &av, &e) == 0 && lv == 800 && av == 600);
    comando_uguale(bn.adb, 2, PH_MSG_VIDEO_RIDIMENSIONA,
                   (const uint8_t[]){0, 0, 0, 1, 0x03, 0x20, 0x02, 0x58, 0x00, 0xf0}, 10);
    VERIFICA(ph_video_ridimensiona(f, 800, 600, 240, NULL, NULL, &e) == 0);
    size_t prima = finto_numero_comandi(bn.adb);
    VERIFICA(ph_video_ridimensiona(f, 801, 600, 240, &lv, &av, &e) == -1);
    VERIFICA(finto_numero_comandi(bn.adb) == prima);
    VERIFICA(ph_video_chiave(f, &e) == 0);
    comando_uguale(bn.adb, 4, PH_MSG_VIDEO_CHIAVE, (const uint8_t[]){0, 0, 0, 1}, 4);

    /* ERRORE del telefono: la sua frase, e il servizio resta vivo */
    PhVideoRichiesta vietata = richiesta("com.vietato");
    VERIFICA(ph_video_apri(bn.c, &vietata, &e) == NULL);
    VERIFICA(strstr(e.testo, "App non trovata sul telefono") != NULL);
    VERIFICA(ph_componente_vivo(bn.c));

    /* jar vecchio: errore chiaro, nessun blocco */
    finto_cfg(bn.adb)->jar_vecchio = true;
    int64_t t0 = ora_ms();
    VERIFICA(ph_video_chiave(f, &e) == -1);
    VERIFICA(strstr(e.testo, "non conosce la richiesta") != NULL && strstr(e.testo, "0x43") != NULL);
    VERIFICA(ph_video_apri(bn.c, &r, &e) == NULL && strstr(e.testo, "non conosce la richiesta") != NULL);
    VERIFICA(ora_ms() - t0 < 2000);
    VERIFICA(ph_componente_vivo(bn.c));
    finto_cfg(bn.adb)->jar_vecchio = false;
    VERIFICA(ph_video_chiave(f, &e) == 0);

    /* tipo che il telefono non conosce -> gestito come ERRORE: provato con un
     * messaggio fatto a mano non e' possibile dall'API, quindi basta il caso sopra */
    ph_video_chiudi(f, false);
    banco_fine(&bn);
}

static void prova_eventi(void)
{
    Banco bn;
    banco_pronto(&bn);
    PhErrore e;
    /* evento spontaneo */
    const uint8_t ev1[] = {PH_MSG_VIDEO_EVENTO, 0, 0, 0, 9, 0, 0, 0, 5, 3, 0x02, 0x80, 0x01, 0xe0};
    VERIFICA(finto_manda_comandi(bn.adb, ev1, sizeof ev1));
    VERIFICA(attendi_eventi(&bn, 1, 2000));
    VERIFICA(bn.ev[0].id == 5 && bn.ev[0].che == 3 && bn.ev[0].a == 640 && bn.ev[0].b == 480);

    /* tipo sconosciuto, evento troppo corto, risposta senza richiesta: ignorati */
    const uint8_t strano[] = {0x77, 0, 0, 0, 2, 9, 9};
    const uint8_t corto[] = {PH_MSG_VIDEO_EVENTO, 0, 0, 0, 3, 0, 0, 1};
    const uint8_t ok_vuoto[] = {PH_MSG_OK, 0, 0, 0, 0};
    const uint8_t errore_solo[] = {PH_MSG_ERRORE, 0, 0, 0, 1, 7};
    VERIFICA(finto_manda_comandi(bn.adb, strano, sizeof strano));
    VERIFICA(finto_manda_comandi(bn.adb, corto, sizeof corto));
    VERIFICA(finto_manda_comandi(bn.adb, ok_vuoto, sizeof ok_vuoto));
    VERIFICA(finto_manda_comandi(bn.adb, errore_solo, sizeof errore_solo));
    VERIFICA(finto_manda_comandi(bn.adb, ev1, sizeof ev1));
    VERIFICA(attendi_eventi(&bn, 2, 2000));
    VERIFICA(numero_eventi(&bn) == 2);

    /* evento tra richiesta e risposta: arrivano tutti e due */
    PhVideoRichiesta r = richiesta("com.a");
    PhVideoFlusso *f = ph_video_apri(bn.c, &r, &e);
    VERIFICA(f != NULL);
    finto_cfg(bn.adb)->evento_prima_di_ok = true;
    VERIFICA(ph_video_chiave(f, &e) == 0);
    VERIFICA(attendi_eventi(&bn, 3, 2000));
    VERIFICA(bn.ev[2].id == 7 && bn.ev[2].che == 2 && bn.ev[2].a == 1 && bn.ev[2].b == 0);
    finto_cfg(bn.adb)->evento_prima_di_ok = false;

    ph_video_chiudi(f, false);
    banco_fine(&bn);
}

static void prova_input_byte(void)
{
    Banco bn;
    banco_pronto(&bn);
    PhErrore e;
    VERIFICA(ph_input_tocco(bn.c, 3, 720, 1280, PH_TOCCO_MUOVI, 100, 200, 5, &e) == 0);
    VERIFICA(ph_input_rotella(bn.c, 3, 720, 1280, 10, 20, -120, 240, &e) == 0);
    VERIFICA(ph_input_tasto(bn.c, 3, true, 66, 0x1000, &e) == 0);
    VERIFICA(ph_input_testo(bn.c, 3, "\xc3\xa8!", &e) == 0);
    VERIFICA(ph_input_indietro(bn.c, 3, &e) == 0);
    VERIFICA(ph_input_tasto(bn.c, 3, false, 66, 0, &e) == 0);
    VERIFICA(finto_attendi_input(bn.adb, 6, 3000));
    uint8_t t, b[64];
    struct {
        uint8_t tipo;
        uint8_t corpo[24];
        size_t n;
    } atteso[] = {
        {PH_MSG_TOCCO, {0, 0, 0, 3, 0x02, 0xd0, 0x05, 0x00, 1, 0, 100, 0, 200, 5}, 14},
        {PH_MSG_ROTELLA, {0, 0, 0, 3, 0x02, 0xd0, 0x05, 0x00, 0, 10, 0, 20, 0xff, 0x88, 0x00, 0xf0}, 16},
        {PH_MSG_TASTO, {0, 0, 0, 3, 0, 0, 0, 0, 66, 0, 0, 0x10, 0}, 13},
        {PH_MSG_TESTO, {0, 0, 0, 3, 0, 3, 0xc3, 0xa8, '!'}, 9},
        {PH_MSG_INDIETRO, {0, 0, 0, 3}, 4},
        {PH_MSG_TASTO, {0, 0, 0, 3, 1, 0, 0, 0, 66, 0, 0, 0, 0}, 13},
    };
    for (size_t i = 0; i < 6; i++) {
        size_t n = finto_input(bn.adb, i, &t, b, sizeof b);
        VERIFICA(t == atteso[i].tipo && n == atteso[i].n && memcmp(b, atteso[i].corpo, n) == 0);
    }
    VERIFICA(canali_aperti(bn.adb, "input", 1));

    /* testo troppo lungo per la stringa (oltre 65535): errore, nulla spedito */
    char *lungo = malloc(70000);
    VERIFICA(lungo != NULL);
    memset(lungo, 'a', 69999);
    lungo[69999] = '\0';
    VERIFICA(ph_input_testo(bn.c, 3, lungo, &e) == -1);
    free(lungo);
    VERIFICA(ph_input_testo(bn.c, 3, NULL, &e) == -1);
    VERIFICA(ph_componente_vivo(bn.c));
    VERIFICA(ph_input_indietro(bn.c, 4, &e) == 0);
    VERIFICA(finto_attendi_input(bn.adb, 7, 3000));
    VERIFICA(finto_numero_input(bn.adb) == 7);

    /* PIZZICO: byte esatti per ogni fase; passo limitato a -1200..1200 */
    for (uint8_t fase = 0; fase <= 3; fase++)
        VERIFICA(ph_input_pizzico(bn.c, 3, 720, 1280, fase, 300, 400, (int16_t)(fase * 10 - 5), &e) == 0);
    VERIFICA(ph_input_pizzico(bn.c, 3, 720, 1280, PH_PIZZICO_CONTINUA, 719, 1279, 30000, &e) == 0);
    VERIFICA(ph_input_pizzico(bn.c, 3, 720, 1280, PH_PIZZICO_CONTINUA, 0, 0, -30000, &e) == 0);
    VERIFICA(finto_attendi_input(bn.adb, 13, 3000));
    for (uint8_t fase = 0; fase <= 3; fase++) {
        uint16_t passo = (uint16_t)(int16_t)(fase * 10 - 5);
        uint8_t atteso[14] = {0, 0, 0, 3, 0x02, 0xd0, 0x05, 0x00, fase, 0x01, 0x2c, 0x01, 0x90,
                              0};
        uint8_t corpo[16];
        size_t n = finto_input(bn.adb, 7 + fase, &t, corpo, sizeof corpo);
        VERIFICA(t == PH_MSG_PIZZICO && n == 15);
        VERIFICA(memcmp(corpo, atteso, 13) == 0);
        VERIFICA(corpo[13] == (uint8_t)(passo >> 8) && corpo[14] == (uint8_t)passo);
    }
    {
        uint8_t corpo[16];
        VERIFICA(finto_input(bn.adb, 11, &t, corpo, sizeof corpo) == 15);
        VERIFICA(corpo[9] == 0x02 && corpo[10] == 0xcf && corpo[11] == 0x04 && corpo[12] == 0xff);
        VERIFICA(corpo[13] == 0x04 && corpo[14] == 0xb0); /* 1200 */
        VERIFICA(finto_input(bn.adb, 12, &t, corpo, sizeof corpo) == 15);
        VERIFICA(corpo[13] == 0xfb && corpo[14] == 0x50); /* -1200 */
    }
    /* argomenti non validi: -1 e nulla spedito */
    VERIFICA(ph_input_pizzico(bn.c, 3, 720, 1280, 4, 1, 1, 0, &e) == -1);
    VERIFICA(ph_input_pizzico(bn.c, 3, 720, 1280, 0, 720, 1, 0, &e) == -1);
    VERIFICA(ph_input_pizzico(bn.c, 3, 720, 1280, 0, 1, 1280, 0, &e) == -1);
    VERIFICA(ph_input_pizzico(bn.c, 3, 0, 1280, 0, 0, 1, 0, &e) == -1);
    VERIFICA(ph_input_pizzico(bn.c, 3, 720, 0, 0, 1, 0, 0, &e) == -1);
    VERIFICA(ph_input_pizzico(NULL, 3, 720, 1280, 0, 1, 1, 0, &e) == -1);
    /* Segnalino: il canale input consegna in ordine, quindi qualunque cosa fosse
     * partita dalle chiamate rifiutate precederebbe il segnalino e sposterebbe il totale. */
    VERIFICA(ph_input_indietro(bn.c, 99, &e) == 0);
    VERIFICA(finto_attendi_input(bn.adb, 14, 3000));
    VERIFICA(finto_numero_input(bn.adb) == 14);
    {
        uint8_t corpo[8];
        VERIFICA(finto_input(bn.adb, 13, &t, corpo, sizeof corpo) == 4);
        VERIFICA(t == PH_MSG_INDIETRO && corpo[0] == 0 && corpo[1] == 0 && corpo[2] == 0 && corpo[3] == 99);
    }
    banco_fine(&bn);
}

typedef struct {
    Banco *bn;
    int indice;
} ArgThread;

#define N_THREAD 4
#define N_EVENTI 250
#define LUNGO_TESTO 3000

static void *scrittore_input(void *p)
{
    ArgThread *a = p;
    char *testo = malloc(LUNGO_TESTO + 1);
    VERIFICA(testo != NULL);
    memset(testo, 'A' + a->indice, LUNGO_TESTO);
    testo[LUNGO_TESTO] = '\0';
    for (int i = 0; i < N_EVENTI; i++) {
        PhErrore e;
        VERIFICA(ph_input_tocco(a->bn->c, (uint32_t)a->indice, 720, 1280, PH_TOCCO_MUOVI, (uint16_t)i,
                                (uint16_t)(i ^ 0x5555), 1, &e) == 0);
        if (i % 25 == 0)
            VERIFICA(ph_input_testo(a->bn->c, (uint32_t)a->indice, testo, &e) == 0);
    }
    free(testo);
    return NULL;
}

static void prova_input_concorrente(void)
{
    Banco bn;
    banco_pronto(&bn);
    pthread_t th[N_THREAD];
    ArgThread arg[N_THREAD];
    for (int i = 0; i < N_THREAD; i++) {
        arg[i] = (ArgThread){&bn, i};
        VERIFICA(pthread_create(&th[i], NULL, scrittore_input, &arg[i]) == 0);
    }
    for (int i = 0; i < N_THREAD; i++)
        pthread_join(th[i], NULL);
    const size_t attesi = (size_t)N_THREAD * (N_EVENTI + N_EVENTI / 25);
    VERIFICA(finto_attendi_input(bn.adb, attesi, 5000));
    VERIFICA(finto_numero_input(bn.adb) == attesi);

    static uint8_t visto[N_THREAD][N_EVENTI];
    static int testi[N_THREAD];
    memset(visto, 0, sizeof visto);
    memset(testi, 0, sizeof testi);
    uint8_t *corpo = malloc(LUNGO_TESTO + 16);
    VERIFICA(corpo != NULL);
    for (size_t i = 0; i < attesi; i++) {
        uint8_t t;
        size_t n = finto_input(bn.adb, i, &t, corpo, LUNGO_TESTO + 16);
        uint32_t id = ((uint32_t)corpo[0] << 24) | ((uint32_t)corpo[1] << 16) | ((uint32_t)corpo[2] << 8) | corpo[3];
        VERIFICA(id < N_THREAD);
        if (t == PH_MSG_TOCCO) {
            VERIFICA(n == 14);
            unsigned x = (unsigned)((corpo[9] << 8) | corpo[10]);
            unsigned y = (unsigned)((corpo[11] << 8) | corpo[12]);
            VERIFICA(x < N_EVENTI && y == (x ^ 0x5555u));
            VERIFICA(visto[id][x] == 0);
            visto[id][x] = 1;
        } else {
            VERIFICA(t == PH_MSG_TESTO && n == 4 + 2 + LUNGO_TESTO);
            for (size_t j = 6; j < n; j++)
                VERIFICA(corpo[j] == (uint8_t)('A' + id));
            testi[id]++;
        }
    }
    free(corpo);
    for (int i = 0; i < N_THREAD; i++)
        VERIFICA(testi[i] == N_EVENTI / 25);
    banco_fine(&bn);
}

typedef struct {
    PhVideoFlusso *f;
    int ok;
} ArgRichieste;

static void *chiedi_chiavi(void *p)
{
    ArgRichieste *a = p;
    for (int i = 0; i < 30; i++) {
        PhErrore e;
        if (ph_video_chiave(a->f, &e) == 0)
            a->ok++;
    }
    return NULL;
}

static void prova_richieste_concorrenti(void)
{
    Banco bn;
    banco_pronto(&bn);
    PhErrore e;
    PhVideoRichiesta r = richiesta("com.a");
    PhVideoFlusso *f = ph_video_apri(bn.c, &r, &e);
    VERIFICA(f != NULL);
    pthread_t th[N_THREAD];
    ArgRichieste arg[N_THREAD];
    for (int i = 0; i < N_THREAD; i++) {
        arg[i] = (ArgRichieste){f, 0};
        VERIFICA(pthread_create(&th[i], NULL, chiedi_chiavi, &arg[i]) == 0);
    }
    /* intanto il telefono manda eventi a raffica */
    const uint8_t ev[] = {PH_MSG_VIDEO_EVENTO, 0, 0, 0, 9, 0, 0, 0, 5, 2, 0, 1, 0, 0};
    for (int i = 0; i < 100; i++) {
        VERIFICA(finto_manda_comandi(bn.adb, ev, sizeof ev));
        pausa_ms(1);
    }
    for (int i = 0; i < N_THREAD; i++) {
        pthread_join(th[i], NULL);
        VERIFICA(arg[i].ok == 30);
    }
    VERIFICA(attendi_eventi(&bn, 100, 3000));
    VERIFICA(numero_eventi(&bn) == 100);
    ph_video_chiudi(f, false);
    banco_fine(&bn);
}

/* ---- Guasti --------------------------------------------------------------*/
static void prova_risposte_rotte(void)
{
    PhErrore e;
    Banco bn;

    banco_pronto(&bn);
    PhVideoRichiesta r = richiesta("com.a");
    PhVideoFlusso *f = ph_video_apri(bn.c, &r, &e);
    VERIFICA(f != NULL);
    finto_cfg(bn.adb)->risposta_rotta = 1; /* troncata e canale chiuso */
    int64_t t0 = ora_ms();
    VERIFICA(ph_video_chiave(f, &e) == -1);
    VERIFICA(ora_ms() - t0 < 3000);
    pausa_ms(20);
    VERIFICA(!ph_componente_vivo(bn.c));
    VERIFICA(ph_video_chiave(f, &e) == -1); /* subito, senza aspettare */
    VERIFICA(ph_input_indietro(bn.c, 1, &e) == -1);
    ph_video_chiudi(f, false);
    banco_fine(&bn);

    banco_pronto(&bn);
    f = ph_video_apri(bn.c, &r, &e);
    VERIFICA(f != NULL);
    finto_cfg(bn.adb)->risposta_rotta = 2; /* lunghezza 2 GiB */
    t0 = ora_ms();
    VERIFICA(ph_video_chiave(f, &e) == -1);
    VERIFICA(ora_ms() - t0 < 3000);
    VERIFICA(!ph_componente_vivo(bn.c));
    ph_video_chiudi(f, false);
    banco_fine(&bn);

    /* il telefono non risponde piu': timeout, poi componente morto */
    banco_pronto(&bn);
    ph_componente_prova_timeout(bn.c, 200);
    finto_cfg(bn.adb)->silenzio = true;
    t0 = ora_ms();
    VERIFICA(ph_video_apri(bn.c, &r, &e) == NULL);
    VERIFICA(strstr(e.testo, "non ha risposto") != NULL);
    VERIFICA(ora_ms() - t0 < 3000);
    VERIFICA(!ph_componente_vivo(bn.c));
    t0 = ora_ms();
    VERIFICA(ph_video_apri(bn.c, &r, &e) == NULL);
    VERIFICA(ora_ms() - t0 < 100);
    banco_fine(&bn);
}

static void *chiedi_chiave_una_volta(void *p)
{
    ArgRichieste *a = p;
    PhErrore e;
    a->ok = ph_video_chiave(a->f, &e);
    return NULL;
}

static void prova_caduta(void)
{
    PhErrore e;
    Banco bn;
    banco_pronto(&bn);
    PhVideoRichiesta r = richiesta("com.a");
    PhVideoFlusso *f = ph_video_apri(bn.c, &r, &e);
    VERIFICA(f != NULL);
    finto_cfg(bn.adb)->silenzio = true;
    ArgRichieste a = {f, 99};
    pthread_t th;
    VERIFICA(pthread_create(&th, NULL, chiedi_chiave_una_volta, &a) == 0);
    pausa_ms(100);
    int64_t t0 = ora_ms();
    finto_cade(bn.adb);
    pthread_join(th, NULL);
    VERIFICA(a.ok == -1);
    VERIFICA(ora_ms() - t0 < 3000);
    VERIFICA(!ph_componente_vivo(bn.c));
    VERIFICA(ph_input_tocco(bn.c, 1, 640, 480, 0, 1, 1, 0, &e) == -1);
    PhVideoPacchetto p;
    VERIFICA(ph_video_leggi(f, &p, 1000, &e) < 0);
    ph_video_chiudi(f, false);
    banco_fine(&bn);

    /* caduta mentre un lettore aspetta pacchetti */
    banco_pronto(&bn);
    finto_cfg(bn.adb)->video_resta_aperto = true;
    f = ph_video_apri(bn.c, &r, &e);
    VERIFICA(f != NULL);
    leggi_pacchetti_normali(f, 6, 640, 480);
    VERIFICA(ph_video_leggi(f, &p, 100, &e) == PH_CANALE_TEMPO);
    VERIFICA(ph_video_leggi(f, &p, 0, &e) == PH_CANALE_TEMPO);
    finto_cade(bn.adb);
    t0 = ora_ms();
    VERIFICA(ph_video_leggi(f, &p, 3000, &e) < 0 && ora_ms() - t0 < 2500);
    ph_video_chiudi(f, false);
    banco_fine(&bn);
}

static void *leggi_bloccato(void *p)
{
    ArgRichieste *a = p;
    PhVideoPacchetto pk;
    PhErrore e;
    a->ok = ph_video_leggi(a->f, &pk, -1, &e);
    return NULL;
}

static void prova_chiusura_sblocca_lettura(void)
{
    PhErrore e;
    Banco bn;
    banco_pronto(&bn);
    finto_cfg(bn.adb)->video_resta_aperto = true;
    PhVideoRichiesta r = richiesta("com.a");
    PhVideoFlusso *f = ph_video_apri(bn.c, &r, &e);
    VERIFICA(f != NULL);
    leggi_pacchetti_normali(f, 6, 640, 480);
    ArgRichieste a = {f, 99};
    pthread_t th;
    VERIFICA(pthread_create(&th, NULL, leggi_bloccato, &a) == 0);
    pausa_ms(150);
    int64_t t0 = ora_ms();
    ph_video_chiudi(f, true);
    pthread_join(th, NULL);
    VERIFICA(a.ok == -1);
    VERIFICA(ora_ms() - t0 < 2000);
    VERIFICA(ph_componente_vivo(bn.c));
    banco_fine(&bn);

    /* il componente si chiude mentre un flusso e' ancora aperto */
    banco_pronto(&bn);
    finto_cfg(bn.adb)->video_resta_aperto = true;
    f = ph_video_apri(bn.c, &r, &e);
    VERIFICA(f != NULL);
    leggi_pacchetti_normali(f, 6, 640, 480);
    ph_componente_chiudi(bn.c);
    bn.c = NULL;
    PhVideoPacchetto p;
    VERIFICA(ph_video_leggi(f, &p, 1000, &e) < 0);
    ph_video_chiudi(f, true); /* non deve toccare il componente chiuso */
    banco_fine(&bn);
}

static void prova_video_malformato(void)
{
    PhErrore e;
    PhVideoPacchetto p;
    PhVideoRichiesta r = richiesta("com.a");
    Banco bn;

    FintoVideoModo rotti[] = {VM_ENORME, VM_TRONCATO, VM_GENERE};
    const char *frasi[] = {"8 MiB", "meta'", "genere"};
    for (int i = 0; i < 3; i++) {
        banco_inizia(&bn);
        VERIFICA(banco_avvia(&bn, 3000, &e) != NULL);
        finto_cfg(bn.adb)->video_modo = rotti[i];
        PhVideoFlusso *f = ph_video_apri(bn.c, &r, &e);
        VERIFICA(f != NULL);
        VERIFICA(ph_video_leggi(f, &p, 3000, &e) == -1);
        VERIFICA(strstr(e.testo, frasi[i]) != NULL);
        VERIFICA(ph_video_leggi(f, &p, 3000, &e) == -1); /* il flusso resta rotto */
        ph_video_chiudi(f, false);
        banco_fine(&bn);
    }

    /* lunghezza falsa: prima nessun dato (tempo scaduto), poi il canale si chiude: errore */
    banco_inizia(&bn);
    VERIFICA(banco_avvia(&bn, 3000, &e) != NULL);
    finto_cfg(bn.adb)->video_modo = VM_LUNGO_FALSO;
    PhVideoFlusso *f = ph_video_apri(bn.c, &r, &e);
    VERIFICA(f != NULL);
    VERIFICA(ph_video_leggi(f, &p, 100, &e) == PH_CANALE_TEMPO);
    VERIFICA(ph_video_leggi(f, &p, 5000, &e) == -1);
    ph_video_chiudi(f, false);
    banco_fine(&bn);

    /* pacchetti validi ma a pezzettini: la lettura riprende dopo ogni tempo scaduto */
    banco_inizia(&bn);
    VERIFICA(banco_avvia(&bn, 3000, &e) != NULL);
    finto_cfg(bn.adb)->video_modo = VM_LENTO;
    f = ph_video_apri(bn.c, &r, &e);
    VERIFICA(f != NULL);
    uint8_t atteso[4096];
    int i = -1, tempi = 0;
    int64_t t0 = ora_ms();
    while (i < 5) {
        int ris = ph_video_leggi(f, &p, 2, &e);
        VERIFICA(ris == 1 || ris == PH_CANALE_TEMPO);
        VERIFICA(ora_ms() - t0 < 20000);
        if (ris == PH_CANALE_TEMPO) {
            tempi++;
            continue;
        }
        size_t n = finto_dati_pacchetto(i, atteso, sizeof atteso);
        VERIFICA(p.configurazione == (i < 0) && p.chiave == (i == 0));
        VERIFICA(p.lunghezza == n && memcmp(p.dati, atteso, n) == 0);
        VERIFICA(p.tempo_us == finto_tempo_pacchetto(i));
        i++;
    }
    VERIFICA(tempi > 0);
    VERIFICA(ph_video_leggi(f, &p, 3000, &e) == 0);
    ph_video_chiudi(f, false);
    banco_fine(&bn);
}

static void prova_pacchetto_grande(void)
{
    Banco bn;
    banco_pronto(&bn);
    PhErrore e;
    finto_cfg(bn.adb)->video_modo = VM_GRANDE;
    PhVideoRichiesta r = richiesta("com.a");
    PhVideoFlusso *f = ph_video_apri(bn.c, &r, &e);
    VERIFICA(f != NULL);
    PhVideoPacchetto p;
    VERIFICA(ph_video_leggi(f, &p, 10000, &e) == 1);
    VERIFICA(!p.configurazione && p.chiave && p.lunghezza == 1536 * 1024 && p.tempo_us == 5555);
    for (size_t j = 0; j < p.lunghezza; j++)
        VERIFICA(p.dati[j] == (uint8_t)(j * 31 + (j >> 8)));
    VERIFICA(ph_video_leggi(f, &p, 3000, &e) == 0);
    ph_video_chiudi(f, false);
    banco_fine(&bn);
}

typedef struct {
    PhComponente *c;
    PhVideoFlusso *f;
    int tipo;
} ArgUso;

static void *usa_componente(void *p)
{
    ArgUso *a = p;
    PhErrore e;
    for (int i = 0; i < 100000; i++) {
        int r = a->tipo == 0 ? ph_video_chiave(a->f, &e)
              : a->tipo == 2 ? ph_input_pizzico(a->c, 1, 640, 480, PH_PIZZICO_CONTINUA, 10, 10, 120, &e)
                             : ph_input_tocco(a->c, 1, 640, 480, PH_TOCCO_MUOVI, 1, 2, 0, &e);
        if (r != 0)
            return NULL; /* componente chiuso: errore e basta */
    }
    return NULL;
}

/* Il componente si chiude mentre altri thread lo stanno usando. Il flusso
 * tiene vivo il componente, quindi i thread non toccano memoria liberata. */
static void prova_chiusura_in_parallelo(void)
{
    for (int giro = 0; giro < 50; giro++) {
        Banco bn;
        banco_inizia(&bn);
        finto_cfg(bn.adb)->chiudi_resta_vivo = true;
        PhErrore ea;
        VERIFICA(banco_avvia(&bn, 3000, &ea) != NULL);
        PhErrore e;
        PhVideoRichiesta r = richiesta("com.a");
        PhVideoFlusso *f = ph_video_apri(bn.c, &r, &e);
        VERIFICA(f != NULL);
        pthread_t th[4];
        ArgUso arg[4] = {{bn.c, f, 0}, {bn.c, f, 1}, {bn.c, f, 1}, {bn.c, f, 2}};
        for (int i = 0; i < 4; i++)
            VERIFICA(pthread_create(&th[i], NULL, usa_componente, &arg[i]) == 0);
        pausa_ms(1 + giro % 8);
        ph_componente_chiudi(bn.c);
        for (int i = 0; i < 4; i++)
            pthread_join(th[i], NULL);
        VERIFICA(ph_video_chiave(f, &e) == -1);
        ph_video_chiudi(f, false);
        bn.c = NULL;
        banco_fine(&bn);
    }
}

/* ---- Funzioni per gli altri pezzi ----------------------------------------*/
static void prova_richiesta_generica(void)
{
    Banco bn;
    banco_pronto(&bn);
    PhErrore e;
    PhBuffer r = PH_BUFFER_VUOTO;
    const uint8_t corpo[] = {1, 2, 3};
    VERIFICA(ph_componente_richiesta(bn.c, 0x70, corpo, 3, &r, &e) == 0);
    VERIFICA(r.lunghezza == 3 && memcmp(r.dati, corpo, 3) == 0);
    comando_uguale(bn.adb, 1, 0x70, corpo, 3);
    VERIFICA(ph_componente_richiesta(bn.c, 0x70, NULL, 0, &r, &e) == 0 && r.lunghezza == 0);
    VERIFICA(ph_componente_richiesta(bn.c, 0x70, corpo, 3, NULL, &e) == 0);
    comando_uguale(bn.adb, 2, 0x70, NULL, 0);
    /* ERRORE del telefono: la sua frase, servizio ancora vivo */
    VERIFICA(ph_componente_richiesta(bn.c, 0x71, NULL, 0, &r, &e) == -1);
    VERIFICA(strstr(e.testo, "dato non disponibile") != NULL && ph_componente_vivo(bn.c));
    /* tipi del componente: rifiutati sul PC, nulla spedito */
    size_t prima = finto_numero_comandi(bn.adb);
    const uint8_t tipi[] = {0x01, 0x02, 0x03, 0x04, 0x40, 0x48, 0x4f, 0x50, 0x54, 0x5f};
    for (size_t i = 0; i < sizeof tipi; i++)
        VERIFICA(ph_componente_richiesta(bn.c, tipi[i], NULL, 0, &r, &e) == -1);
    VERIFICA(finto_numero_comandi(bn.adb) == prima);
    /* tipo che il telefono non conosce */
    VERIFICA(ph_componente_richiesta(bn.c, 0x7a, NULL, 0, &r, &e) == -1);
    VERIFICA(strstr(e.testo, "non conosce la richiesta") != NULL);
    ph_buffer_libera(&r);
    banco_fine(&bn);
}

typedef struct {
    atomic_int chiamate;
    atomic_bool tolto;
    atomic_int violazioni;
    uint8_t tipo;
    uint8_t corpo[8];
    size_t n;
    void *utente_atteso;
} Ascolto;

static void ascoltatore(uint8_t tipo, const uint8_t *corpo, size_t n, void *utente)
{
    Ascolto *a = utente;
    if (atomic_load(&a->tolto))
        atomic_fetch_add(&a->violazioni, 1);
    a->tipo = tipo;
    a->n = n < 8 ? n : 8;
    memcpy(a->corpo, corpo, a->n);
    atomic_fetch_add(&a->chiamate, 1);
}

static bool attendi_chiamate(Ascolto *a, int n)
{
    for (int t = 0; t < 3000; t += 2) {
        if (atomic_load(&a->chiamate) >= n)
            return true;
        pausa_ms(2);
    }
    return false;
}

static void prova_ascoltatori(void)
{
    Banco bn;
    banco_pronto(&bn);
    PhErrore e;
    static Ascolto a;
    atomic_init(&a.chiamate, 0);
    atomic_init(&a.tolto, false);
    atomic_init(&a.violazioni, 0);
    VERIFICA(ph_componente_ascolta(bn.c, 0x7e, 0x7f, ascoltatore, &a, &e) == 0);
    const uint8_t ev[] = {0x7f, 0, 0, 0, 3, 9, 8, 7};
    const uint8_t altro[] = {0x7d, 0, 0, 0, 1, 1};
    VERIFICA(finto_manda_comandi(bn.adb, altro, sizeof altro)); /* non registrato: ignorato */
    VERIFICA(finto_manda_comandi(bn.adb, ev, sizeof ev));
    VERIFICA(attendi_chiamate(&a, 1));
    pausa_ms(20);
    VERIFICA(atomic_load(&a.chiamate) == 1 && a.tipo == 0x7f && a.n == 3 && memcmp(a.corpo, ev + 5, 3) == 0);
    /* evento con corpo vuoto */
    const uint8_t vuoto[] = {0x7e, 0, 0, 0, 0};
    VERIFICA(finto_manda_comandi(bn.adb, vuoto, sizeof vuoto));
    VERIFICA(attendi_chiamate(&a, 2) && a.tipo == 0x7e && a.n == 0);

    /* intervalli non ammessi, massimo 8, sostituzione e rimozione */
    VERIFICA(ph_componente_ascolta(bn.c, 0x10, 0x05, ascoltatore, &a, &e) == -1);
    VERIFICA(ph_componente_ascolta(bn.c, 0x01, 0x10, ascoltatore, &a, &e) == -1);
    VERIFICA(ph_componente_ascolta(bn.c, 0x48, 0x48, ascoltatore, &a, &e) == -1);
    for (uint8_t t = 0x60; t < 0x67; t++)
        VERIFICA(ph_componente_ascolta(bn.c, t, t, ascoltatore, &a, &e) == 0);
    VERIFICA(ph_componente_ascolta(bn.c, 0x70, 0x70, ascoltatore, &a, &e) == -1);
    VERIFICA(ph_componente_ascolta(bn.c, 0x60, 0x60, ascoltatore, &a, &e) == 0); /* sostituisce */
    VERIFICA(ph_componente_ascolta(bn.c, 0x60, 0x60, NULL, NULL, &e) == 0);
    VERIFICA(ph_componente_ascolta(bn.c, 0x70, 0x70, ascoltatore, &a, &e) == 0); /* ora c'e' posto */
    VERIFICA(ph_componente_ascolta(bn.c, 0x33, 0x34, NULL, NULL, &e) == 0);      /* togliere cio' che non c'e': niente */
    for (uint8_t t = 0x60; t < 0x67; t++)
        VERIFICA(ph_componente_ascolta(bn.c, t, t, NULL, NULL, &e) == 0);
    VERIFICA(ph_componente_ascolta(bn.c, 0x70, 0x70, NULL, NULL, &e) == 0);

    /* intervalli che si toccano senza coincidere: rifiutati; lo stesso identico sostituisce */
    VERIFICA(ph_componente_ascolta(bn.c, 0x70, 0x72, ascoltatore, &a, &e) == 0);
    VERIFICA(ph_componente_ascolta(bn.c, 0x71, 0x73, ascoltatore, &a, &e) == -1);
    VERIFICA(ph_componente_ascolta(bn.c, 0x72, 0x72, ascoltatore, &a, &e) == -1);
    VERIFICA(ph_componente_ascolta(bn.c, 0x6f, 0x75, ascoltatore, &a, &e) == -1);
    VERIFICA(ph_componente_ascolta(bn.c, 0x70, 0x72, ascoltatore, &a, &e) == 0);
    VERIFICA(ph_componente_ascolta(bn.c, 0x73, 0x74, ascoltatore, &a, &e) == 0);
    VERIFICA(ph_componente_ascolta(bn.c, 0x70, 0x72, NULL, NULL, &e) == 0);
    VERIFICA(ph_componente_ascolta(bn.c, 0x73, 0x74, NULL, NULL, &e) == 0);
    banco_fine(&bn);
}

/* Ascoltatore che si ferma a meta' chiamata finche' il test non da' il via. */
typedef struct {
    PhComponente *c;
    atomic_int chiamate;
    atomic_int fermati_a;   /* all'evento con questo numero si ferma (0 = mai) */
    atomic_bool dentro;
    atomic_bool via;
    atomic_bool tornato;
    atomic_int segnalini;
    bool si_toglie;         /* alla prima chiamata toglie se stesso */
} Cancello;

static void ascoltatore_cancello(uint8_t tipo, const uint8_t *corpo, size_t n, void *utente)
{
    (void)tipo; (void)corpo; (void)n;
    Cancello *g = utente;
    int numero = atomic_fetch_add(&g->chiamate, 1) + 1;
    if (g->si_toglie) {
        PhErrore e;
        VERIFICA(ph_componente_ascolta(g->c, 0x7e, 0x7f, NULL, NULL, &e) == 0);
    }
    if (numero == atomic_load(&g->fermati_a)) {
        atomic_store(&g->dentro, true);
        for (int t = 0; t < 5000 && !atomic_load(&g->via); t++)
            pausa_ms(1);
    }
}

static void ascoltatore_segnalino(uint8_t tipo, const uint8_t *corpo, size_t n, void *utente)
{
    (void)tipo; (void)corpo; (void)n;
    atomic_fetch_add(&((Cancello *)utente)->segnalini, 1);
}

static void *toglie_ascoltatore(void *p)
{
    Cancello *g = p;
    PhErrore e;
    VERIFICA(ph_componente_ascolta(g->c, 0x7e, 0x7f, NULL, NULL, &e) == 0);
    atomic_store(&g->tornato, true);
    return NULL;
}

static void manda_n(PhAdb *adb, uint8_t tipo, int n)
{
    const uint8_t ev[] = {tipo, 0, 0, 0, 1, 5};
    for (int i = 0; i < n; i++)
        VERIFICA(finto_manda_comandi(adb, ev, sizeof ev));
}

static void attendi_segnalini(Cancello *g, int n)
{
    for (int t = 0; t < 3000 && atomic_load(&g->segnalini) < n; t += 2)
        pausa_ms(2);
    VERIFICA(atomic_load(&g->segnalini) >= n);
}

static void prova_ascoltatore_tolto(void)
{
    PhErrore e;
    Banco bn;

    /* tolto da un altro thread mentre l'ascoltatore e' dentro la chiamata */
    banco_pronto(&bn);
    static Cancello g;
    memset((void *)&g, 0, sizeof g);
    g.c = bn.c;
    atomic_init(&g.fermati_a, 3);
    VERIFICA(ph_componente_ascolta(bn.c, 0x7e, 0x7f, ascoltatore_cancello, &g, &e) == 0);
    VERIFICA(ph_componente_ascolta(bn.c, 0x7d, 0x7d, ascoltatore_segnalino, &g, &e) == 0);
    manda_n(bn.adb, 0x7f, 3);
    for (int t = 0; t < 3000 && !atomic_load(&g.dentro); t += 2)
        pausa_ms(2);
    VERIFICA(atomic_load(&g.dentro));
    pthread_t th;
    VERIFICA(pthread_create(&th, NULL, toglie_ascoltatore, &g) == 0);
    pausa_ms(100);
    VERIFICA(!atomic_load(&g.tornato)); /* aspetta che la chiamata in corso finisca */
    atomic_store(&g.via, true);
    pthread_join(th, NULL);
    VERIFICA(atomic_load(&g.tornato));
    manda_n(bn.adb, 0x7f, 5);
    manda_n(bn.adb, 0x7d, 1); /* segnalino: gli eventi di prima sono stati tutti letti */
    attendi_segnalini(&g, 1);
    VERIFICA(atomic_load(&g.chiamate) == 3);
    banco_fine(&bn);

    /* l'ascoltatore toglie se stesso dentro la chiamata */
    banco_pronto(&bn);
    memset((void *)&g, 0, sizeof g);
    g.c = bn.c;
    g.si_toglie = true;
    VERIFICA(ph_componente_ascolta(bn.c, 0x7e, 0x7f, ascoltatore_cancello, &g, &e) == 0);
    VERIFICA(ph_componente_ascolta(bn.c, 0x7d, 0x7d, ascoltatore_segnalino, &g, &e) == 0);
    manda_n(bn.adb, 0x7f, 4);
    manda_n(bn.adb, 0x7d, 1);
    attendi_segnalini(&g, 1);
    VERIFICA(atomic_load(&g.chiamate) == 1);
    banco_fine(&bn);
}

static void prova_canale_audio(void)
{
    Banco bn;
    banco_pronto(&bn);
    PhErrore e;
    VERIFICA(ph_componente_canale(bn.c, "", &e) == NULL);
    VERIFICA(ph_componente_canale(bn.c, "a\nb", &e) == NULL);
    VERIFICA(ph_componente_canale(bn.c, NULL, &e) == NULL);
    VERIFICA(ph_componente_canale(bn.c, "comandi", &e) == NULL);
    VERIFICA(ph_componente_canale(bn.c, "input", &e) == NULL);
    VERIFICA(ph_componente_canale(bn.c, "video:3", &e) == NULL);
    VERIFICA(finto_canali_aperti(bn.adb, "comandi") == 1 && finto_canali_aperti(bn.adb, "video:") == 0);
    PhCanale *k = ph_componente_canale(bn.c, "audio", &e);
    VERIFICA(k != NULL);
    uint8_t d[8];
    VERIFICA(ph_canale_leggi_tutto(k, d, 8, 2000, &e) == 0);
    VERIFICA(d[0] == 1 && d[7] == 8);
    VERIFICA(canali_aperti(bn.adb, "audio", 1));
    /* il canale resta aperto: la chiusura del componente non si blocca */
    int64_t t0 = ora_ms();
    ph_componente_chiudi(bn.c);
    bn.c = NULL;
    VERIFICA(ora_ms() - t0 < 3000);
    ph_canale_chiudi(k);
    banco_fine(&bn);

    /* componente morto: niente canali nuovi */
    banco_pronto(&bn);
    finto_cade(bn.adb);
    for (int t = 0; t < 2000 && ph_componente_vivo(bn.c); t += 5)
        pausa_ms(5);
    VERIFICA(!ph_componente_vivo(bn.c));
    VERIFICA(ph_componente_canale(bn.c, "audio", &e) == NULL);
    banco_fine(&bn);
}

/* Legge pacchetti finche' ne arriva uno con il byte dato (o scade il tempo). */
static bool leggi_fino_a(PhVideoFlusso *f, uint8_t byte, int ms, PhVideoPacchetto *p)
{
    int64_t t0 = ora_ms();
    PhErrore e;
    while (ora_ms() - t0 < ms) {
        int r = ph_video_leggi(f, p, 20, &e);
        if (r == 1 && p->lunghezza == 20 && p->dati[0] == byte)
            return true;
        if (r < 0 && r != PH_CANALE_TEMPO)
            return false;
    }
    return false;
}

/* Svuota quel che e' gia' in arrivo; torna quanti pacchetti ha letto. */
static int svuota(PhVideoFlusso *f)
{
    PhVideoPacchetto p;
    PhErrore e;
    int n = 0;
    while (ph_video_leggi(f, &p, 30, &e) == 1)
        n++;
    return n;
}

static void prova_pausa_e_ripresa(void)
{
    Banco bn;
    banco_inizia(&bn);
    finto_cfg(bn.adb)->video_continuo = true;
    PhErrore e;
    VERIFICA(banco_avvia(&bn, 3000, &e) != NULL);
    PhVideoRichiesta r = richiesta("com.a");
    PhVideoFlusso *f = ph_video_apri(bn.c, &r, &e);
    VERIFICA(f != NULL);
    PhVideoPacchetto p;
    VERIFICA(leggi_fino_a(f, 0xdd, 3000, &p)); /* il flusso scorre */

    VERIFICA(ph_video_pausa(f, &e) == 0);
    comando_uguale(bn.adb, 2, PH_MSG_VIDEO_PAUSA, (const uint8_t[]){0, 0, 0, 1}, 4);
    VERIFICA(ph_video_pausa(f, &e) == 0); /* due volte di fila: nessun errore */
    (void)svuota(f);
    VERIFICA(ph_video_leggi(f, &p, 100, &e) == PH_CANALE_TEMPO); /* in pausa: nessun pacchetto */
    VERIFICA(ph_video_leggi(f, &p, 0, &e) == PH_CANALE_TEMPO);

    /* in pausa RIDIMENSIONA e CHIAVE sono rifiutati dal telefono, l'input passa */
    uint16_t lv, av;
    VERIFICA(ph_video_ridimensiona(f, 800, 600, 240, &lv, &av, &e) == -1);
    VERIFICA(strstr(e.testo, "sessione in pausa") != NULL);
    VERIFICA(ph_video_chiave(f, &e) == -1 && strstr(e.testo, "sessione in pausa") != NULL);
    VERIFICA(ph_componente_vivo(bn.c));
    VERIFICA(ph_input_indietro(bn.c, ph_video_id(f), &e) == 0);

    VERIFICA(ph_video_riprendi(f, &e) == 0);
    comando_uguale(bn.adb, 6, PH_MSG_VIDEO_RIPRENDI, (const uint8_t[]){0, 0, 0, 1}, 4);
    VERIFICA(ph_video_riprendi(f, &e) == 0);
    VERIFICA(leggi_fino_a(f, 0xee, 3000, &p) && p.chiave); /* il primo dopo la ripresa e' chiave */
    VERIFICA(ph_video_chiave(f, &e) == 0);

    /* una lettura bloccata su un altro thread non e' disturbata dalla pausa */
    ArgRichieste a = {f, 99};
    pthread_t th;
    VERIFICA(ph_video_pausa(f, &e) == 0);
    (void)svuota(f);
    VERIFICA(pthread_create(&th, NULL, leggi_bloccato, &a) == 0);
    pausa_ms(100);
    VERIFICA(ph_video_riprendi(f, &e) == 0);
    pthread_join(th, NULL);
    VERIFICA(a.ok == 1);

    /* ERRORE del telefono: la sua frase */
    finto_cfg(bn.adb)->pausa_rifiuta = true;
    VERIFICA(ph_video_pausa(f, &e) == -1 && strstr(e.testo, "pausa non riuscita") != NULL);
    VERIFICA(ph_video_riprendi(f, &e) == -1 && strstr(e.testo, "pausa non riuscita") != NULL);
    VERIFICA(ph_componente_vivo(bn.c));
    finto_cfg(bn.adb)->pausa_rifiuta = false;

    /* componente morto: errore subito */
    finto_cade(bn.adb);
    for (int t = 0; t < 2000 && ph_componente_vivo(bn.c); t += 5)
        pausa_ms(5);
    int64_t t0 = ora_ms();
    VERIFICA(ph_video_pausa(f, &e) == -1 && ph_video_riprendi(f, &e) == -1);
    VERIFICA(ora_ms() - t0 < 500);
    VERIFICA(ph_video_pausa(NULL, &e) == -1);
    ph_video_chiudi(f, false);
    banco_fine(&bn);
}

/* Un thread (T) fa richieste a ripetizione; il principale chiude il flusso su
 * un altro thread (C). Il finto trattiene la risposta a VIDEO_CHIUDI finche'
 * il test non apre il cancello: ph_video_chiudi non puo' tornare (e liberare)
 * prima che T abbia finito, a ogni giro. T si ferma al primo -1. */
typedef struct {
    PhVideoFlusso *f;
    int modo;           /* 0 pausa/riprendi, 1 chiave, 2 ridimensiona */
    int ris;
    atomic_int fatte;
    atomic_bool finito;
} ArgPausa;

static void *richieste_in_corsa(void *p)
{
    ArgPausa *a = p;
    PhErrore e;
    for (int i = 0;; i++) {
        if (a->modo == 0)
            a->ris = i % 2 == 0 ? ph_video_pausa(a->f, &e) : ph_video_riprendi(a->f, &e);
        else if (a->modo == 1)
            a->ris = ph_video_chiave(a->f, &e);
        else
            a->ris = ph_video_ridimensiona(a->f, 640, 480, 320, NULL, NULL, &e);
        if (a->ris != 0)
            break;
        atomic_fetch_add(&a->fatte, 1);
    }
    atomic_store(&a->finito, true);
    return NULL;
}

typedef struct {
    PhVideoFlusso *f;
    atomic_bool tornato;
} ArgChiudi;

static void *chiudi_su_thread(void *p)
{
    ArgChiudi *a = p;
    ph_video_chiudi(a->f, false);
    atomic_store(&a->tornato, true);
    return NULL;
}

/* Join con scadenza: se il thread non finisce il test fallisce, non si blocca. */
static void unisci(pthread_t th, int secondi)
{
    struct timespec t;
    clock_gettime(CLOCK_REALTIME, &t);
    t.tv_sec += secondi;
    VERIFICA(pthread_timedjoin_np(th, NULL, &t) == 0);
}

/* Nessuna richiesta del flusso dopo la sua VIDEO_CHIUDI. */
static void nessuna_richiesta_dopo_chiusura(PhAdb *adb, uint32_t id)
{
    size_t n = finto_numero_comandi(adb);
    size_t chiusura = n;
    for (size_t k = 0; k < n; k++) {
        uint8_t tipo, corpo[32];
        size_t len = finto_comando(adb, k, &tipo, corpo, sizeof corpo);
        if (tipo == PH_MSG_VIDEO_CHIUDI && len >= 4)
            chiusura = k;
        if (k > chiusura && tipo >= PH_MSG_VIDEO_RIDIMENSIONA && tipo <= PH_MSG_VIDEO_RIPRENDI && len >= 4) {
            uint32_t i2 = ((uint32_t)corpo[0] << 24) | ((uint32_t)corpo[1] << 16) | ((uint32_t)corpo[2] << 8) | corpo[3];
            VERIFICA(i2 != id);
        }
    }
    VERIFICA(chiusura < n);
}

static void prova_richieste_su_flusso_in_chiusura(void)
{
    for (int giro = 0; giro < 12; giro++) {
        Banco bn;
        banco_pronto(&bn);
        PhErrore e;
        PhVideoRichiesta r = richiesta("com.a");
        PhVideoFlusso *f = ph_video_apri(bn.c, &r, &e);
        VERIFICA(f != NULL);
        const uint32_t id = ph_video_id(f);
        finto_trattieni(bn.adb, PH_MSG_VIDEO_CHIUDI);
        ArgPausa a;
        memset((void *)&a, 0, sizeof a);
        a.f = f;
        a.modo = giro % 3;
        pthread_t t, c;
        VERIFICA(pthread_create(&t, NULL, richieste_in_corsa, &a) == 0);
        while (atomic_load(&a.fatte) < 3)
            pausa_ms(1);
        ArgChiudi ac;
        memset((void *)&ac, 0, sizeof ac);
        ac.f = f;
        VERIFICA(pthread_create(&c, NULL, chiudi_su_thread, &ac) == 0);
        unisci(t, 10); /* T esce per il rifiuto (flusso in chiusura) */
        VERIFICA(a.ris == -1);
        VERIFICA(!atomic_load(&ac.tornato)); /* C e' fermo: il telefono non ha risposto a VIDEO_CHIUDI */
        finto_apri_cancello(bn.adb);
        unisci(c, 10);
        nessuna_richiesta_dopo_chiusura(bn.adb, id);
        banco_fine(&bn);
    }
}

static void *pausa_una_volta(void *p)
{
    ArgPausa *a = p;
    PhErrore e;
    a->ris = ph_video_pausa(a->f, &e);
    atomic_store(&a->finito, true);
    return NULL;
}

/* La chiusura aspetta una richiesta in corso: VIDEO_CHIUDI parte solo dopo che
 * la PAUSA trattenuta dal finto ha avuto risposta. */
static void prova_chiusura_aspetta_richiesta_in_corso(void)
{
    Banco bn;
    banco_pronto(&bn);
    PhErrore e;
    PhVideoRichiesta r = richiesta("com.a");
    PhVideoFlusso *f = ph_video_apri(bn.c, &r, &e);
    VERIFICA(f != NULL);
    finto_trattieni(bn.adb, PH_MSG_VIDEO_PAUSA);
    ArgPausa a;
    memset((void *)&a, 0, sizeof a);
    a.f = f;
    pthread_t t, c;
    VERIFICA(pthread_create(&t, NULL, pausa_una_volta, &a) == 0);
    VERIFICA(finto_attendi_trattenuta(bn.adb, 3000)); /* la PAUSA e' ferma al cancello */
    size_t prima = finto_numero_comandi(bn.adb);
    ArgChiudi ac;
    memset((void *)&ac, 0, sizeof ac);
    ac.f = f;
    VERIFICA(pthread_create(&c, NULL, chiudi_su_thread, &ac) == 0);
    pausa_ms(100); /* attesa breve dichiarata: C ha il tempo di arrivare a ph_video_chiudi */
    VERIFICA(finto_numero_comandi(bn.adb) == prima); /* nessun VIDEO_CHIUDI ancora */
    VERIFICA(!atomic_load(&ac.tornato) && !atomic_load(&a.finito));
    finto_apri_cancello(bn.adb);
    unisci(t, 10);
    VERIFICA(a.ris == 0);
    unisci(c, 10);
    VERIFICA(finto_numero_comandi(bn.adb) == prima + 1);
    uint8_t tipo, corpo[8];
    VERIFICA(finto_comando(bn.adb, prima, &tipo, corpo, sizeof corpo) == 5 && tipo == PH_MSG_VIDEO_CHIUDI);
    nessuna_richiesta_dopo_chiusura(bn.adb, 1);
    banco_fine(&bn);
}

/* ---- Presenza automatica -------------------------------------------------*/
/* Aspetta (con scadenza) che il servizio abbia ricevuto almeno n richieste di quel tipo. */
static bool attendi_conta(PhAdb *adb, uint8_t tipo, int n, int ms)
{
    for (int t = 0; t < ms; t += 2) {
        if (finto_conta_comandi(adb, tipo) >= n)
            return true;
        pausa_ms(2);
    }
    return finto_conta_comandi(adb, tipo) >= n;
}

static void prova_presenza_dopo_silenzio(void)
{
    Banco bn;
    banco_pronto(&bn);
    VERIFICA(finto_conta_comandi(bn.adb, PH_MSG_PRESENZA) == 0);
    ph_componente_prova_presenza(bn.c, 150);
    VERIFICA(attendi_conta(bn.adb, PH_MSG_PRESENZA, 1, 5000));
    uint8_t t, corpo[4];
    size_t n = finto_numero_comandi(bn.adb);
    VERIFICA(finto_comando(bn.adb, n - 1, &t, corpo, sizeof corpo) == 0 && t == PH_MSG_PRESENZA);
    /* si ripete a ogni intervallo di silenzio e il componente resta vivo */
    VERIFICA(attendi_conta(bn.adb, PH_MSG_PRESENZA, 3, 5000));
    VERIFICA(ph_componente_vivo(bn.c));
    banco_fine(&bn);
}

/* Traffico piu' frequente dell'intervallo: nessuna PRESENZA; poi, finito il
 * traffico, arriva non prima di un intervallo dall'ultima richiesta. */
static void prova_presenza_con_traffico(void)
{
    Banco bn;
    banco_pronto(&bn);
    PhErrore e;
    ph_componente_prova_presenza(bn.c, 600);
    int64_t ultima = 0;
    for (int i = 0; i < 40; i++) {
        ultima = ora_ms();
        VERIFICA(ph_componente_richiesta(bn.c, 0x70, NULL, 0, NULL, &e) == 0);
        pausa_ms(50); /* ritmo del traffico: non e' un'attesa di un altro thread */
    }
    VERIFICA(finto_conta_comandi(bn.adb, PH_MSG_PRESENZA) == 0);
    VERIFICA(attendi_conta(bn.adb, PH_MSG_PRESENZA, 1, 5000));
    VERIFICA(ora_ms() - ultima >= 590); /* il conto era ripartito dall'ultima richiesta */
    banco_fine(&bn);
}

static void prova_presenza_chiusura_durante_attesa(void)
{
    Banco bn;
    banco_pronto(&bn); /* intervallo vero: 300 s */
    int64_t t0 = ora_ms();
    ph_componente_chiudi(bn.c);
    bn.c = NULL;
    VERIFICA(ora_ms() - t0 < 1500); /* il thread si sveglia subito */
    VERIFICA(finto_conta_comandi(bn.adb, PH_MSG_PRESENZA) == 0);
    banco_fine(&bn);
}

static void *chiudi_componente(void *p)
{
    Banco *bn = p;
    ph_componente_chiudi(bn->c);
    return NULL;
}

/* Chiusura durante una PRESENZA lenta: finisce entro il limite e dopo non ne parte nessuna. */
static void prova_presenza_chiusura_durante_presenza_lenta(void)
{
    Banco bn;
    banco_pronto(&bn);
    finto_trattieni(bn.adb, PH_MSG_PRESENZA);
    ph_componente_prova_presenza(bn.c, 50);
    VERIFICA(finto_attendi_trattenuta(bn.adb, 5000)); /* la PRESENZA e' ferma al cancello */
    pthread_t c;
    VERIFICA(pthread_create(&c, NULL, chiudi_componente, &bn) == 0);
    pausa_ms(100); /* attesa breve dichiarata: la chiusura e' in coda dietro la PRESENZA */
    VERIFICA(finto_conta_comandi(bn.adb, PH_MSG_CHIUDI) == 0);
    finto_apri_cancello(bn.adb);
    unisci(c, 10);
    bn.c = NULL;
    /* nessuna PRESENZA dopo CHIUDI */
    size_t n = finto_numero_comandi(bn.adb), chiusura = n;
    for (size_t k = 0; k < n; k++) {
        uint8_t tipo, corpo[4];
        (void)finto_comando(bn.adb, k, &tipo, corpo, sizeof corpo);
        if (tipo == PH_MSG_CHIUDI)
            chiusura = k;
        VERIFICA(!(k > chiusura && tipo == PH_MSG_PRESENZA));
    }
    VERIFICA(chiusura < n);
    banco_fine(&bn);
}

/* La scadenza della PRESENZA passa mentre CHIUDI aspetta la risposta: dopo
 * CHIUDI non deve partire nessuna PRESENZA (il finto qui resta vivo, cosi'
 * la vedrebbe). */
static void prova_presenza_non_dopo_chiudi(void)
{
    for (int giro = 0; giro < 10; giro++) {
    Banco bn;
    banco_inizia(&bn);
    finto_cfg(bn.adb)->ritardo_chiudi_ms = 80;
    finto_cfg(bn.adb)->chiudi_resta_vivo = true;
    PhErrore e;
    VERIFICA(banco_avvia(&bn, 3000, &e) != NULL);
    ph_componente_prova_presenza(bn.c, 50);
    VERIFICA(attendi_conta(bn.adb, PH_MSG_PRESENZA, 1, 5000));
    ph_componente_chiudi(bn.c);
    bn.c = NULL;
    size_t n = finto_numero_comandi(bn.adb), chiusura = n;
    for (size_t k = 0; k < n; k++) {
        uint8_t tipo, corpo[4];
        (void)finto_comando(bn.adb, k, &tipo, corpo, sizeof corpo);
        if (tipo == PH_MSG_CHIUDI)
            chiusura = k;
        VERIFICA(!(k > chiusura && tipo == PH_MSG_PRESENZA));
    }
    VERIFICA(chiusura < n);
    banco_fine(&bn);
    }
}

/* Il PC si sospende: l'orologio della presenza salta avanti e la PRESENZA parte subito. */
static atomic_llong salto_boot_ms;

static int64_t boot_finto(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (int64_t)t.tv_sec * 1000 + t.tv_nsec / 1000000 + atomic_load(&salto_boot_ms);
}

static void prova_presenza_dopo_sospensione(void)
{
    /* logica pura */
    VERIFICA(ph_presenza_resta_ms(1000, 300000, 1000) == 300000);
    VERIFICA(ph_presenza_resta_ms(1000, 300000, 301000) == 0);
    VERIFICA(ph_presenza_resta_ms(1000, 300000, 900000) == 0);
    VERIFICA(ph_presenza_resta_ms(0, 100, -5) == 105);
    VERIFICA(ph_presenza_fetta_ms(5) == 5 && ph_presenza_fetta_ms(100000) == ph_presenza_fetta_max_ms);

    /* con l'orologio sostituito: intervallo lungo, nessuna PRESENZA, poi salto di 10 minuti */
    atomic_store(&salto_boot_ms, 0);
    ph_boot_hook = boot_finto;
    ph_presenza_fetta_max_ms = 20;
    Banco bn;
    banco_pronto(&bn);
    ph_componente_prova_presenza(bn.c, 300000);
    VERIFICA(finto_conta_comandi(bn.adb, PH_MSG_PRESENZA) == 0);
    atomic_store(&salto_boot_ms, 600000);
    VERIFICA(attendi_conta(bn.adb, PH_MSG_PRESENZA, 1, 5000));
    banco_fine(&bn);
    ph_boot_hook = NULL;
    ph_presenza_fetta_max_ms = 30000;
}

/* Chiusura con il telefono collegato ma muto e una richiesta ferma: finisce
 * entro il limite e nessuna richiesta resta appesa. */
static void *richiesta_ferma(void *p)
{
    ArgRichieste *a = p;
    PhErrore e;
    a->ok = ph_componente_richiesta((PhComponente *)a->f, 0x70, NULL, 0, NULL, &e);
    return NULL;
}

static void prova_chiusura_con_richiesta_ferma(void)
{
    Banco bn;
    banco_pronto(&bn);
    /* Un flusso aperto tiene valida la struttura del componente finche' il
     * thread della richiesta non e' uscito (altrimenti sarebbe uso dopo liberazione). */
    PhErrore e0;
    PhVideoRichiesta r0 = richiesta("com.a");
    PhVideoFlusso *tenuta = ph_video_apri(bn.c, &r0, &e0);
    VERIFICA(tenuta != NULL);
    finto_trattieni(bn.adb, 0x70);
    ArgRichieste a = {(PhVideoFlusso *)bn.c, 99};
    pthread_t t;
    VERIFICA(pthread_create(&t, NULL, richiesta_ferma, &a) == 0);
    VERIFICA(finto_attendi_trattenuta(bn.adb, 5000)); /* la richiesta e' ferma al cancello */
    int64_t t0 = ora_ms();
    ph_componente_chiudi(bn.c);
    bn.c = NULL;
    VERIFICA(ora_ms() - t0 < 6000); /* ~2 s di turno + chiusura, non 15 s + 2 s */
    unisci(t, 10); /* la richiesta e' stata svegliata dalla chiusura */
    VERIFICA(a.ok == -1);
    ph_video_chiudi(tenuta, false);
    banco_fine(&bn);
}

/* ERRORE del telefono: nessun danno, si riprova; nessuna risposta: vale la regola di ogni richiesta. */
static void prova_presenza_risposte_cattive(void)
{
    Banco bn;
    banco_pronto(&bn);
    finto_cfg(bn.adb)->presenza_errore = true;
    ph_componente_prova_presenza(bn.c, 100);
    VERIFICA(attendi_conta(bn.adb, PH_MSG_PRESENZA, 3, 5000));
    VERIFICA(ph_componente_vivo(bn.c));
    PhErrore e;
    VERIFICA(ph_componente_richiesta(bn.c, 0x70, NULL, 0, NULL, &e) == 0); /* il canale resta allineato */
    banco_fine(&bn);

    banco_pronto(&bn);
    ph_componente_prova_timeout(bn.c, 200);
    finto_cfg(bn.adb)->silenzio = true;
    ph_componente_prova_presenza(bn.c, 100);
    bool morto = false;
    for (int t = 0; t < 5000 && !morto; t += 5) {
        morto = !ph_componente_vivo(bn.c);
        if (!morto)
            pausa_ms(5);
    }
    VERIFICA(morto);
    banco_fine(&bn);
}

/* Messaggi con la cornice giusta ma contenuto qualunque: il componente non
 * deve cadere ne' confondere la richiesta successiva. */
static void prova_spazzatura(void)
{
    Banco bn;
    banco_pronto(&bn);
    PhErrore e;
    unsigned seme = 777;
    for (int i = 0; i < 300; i++) {
        uint8_t m[5 + 24];
        seme = seme * 1103515245u + 12345u;
        size_t n = (seme >> 16) % 24;
        seme = seme * 1103515245u + 12345u;
        m[0] = (uint8_t)(seme >> 16);
        m[1] = m[2] = m[3] = 0;
        m[4] = (uint8_t)n;
        for (size_t j = 0; j < n; j++) {
            seme = seme * 1103515245u + 12345u;
            m[5 + j] = (uint8_t)(seme >> 16);
        }
        VERIFICA(finto_manda_comandi(bn.adb, m, 5 + n));
    }
    /* segnalino: quando arriva, tutto il resto e' stato letto */
    const uint8_t fine[] = {PH_MSG_VIDEO_EVENTO, 0, 0, 0, 9, 0, 0, 0x03, 0xe7, 4, 0, 0, 0, 0};
    VERIFICA(finto_manda_comandi(bn.adb, fine, sizeof fine));
    bool trovato = false;
    for (int t = 0; t < 3000 && !trovato; t += 5) {
        pthread_mutex_lock(&bn.m);
        for (int i = 0; i < bn.nev; i++)
            if (bn.ev[i].id == 999 && bn.ev[i].che == 4)
                trovato = true;
        pthread_mutex_unlock(&bn.m);
        if (!trovato)
            pausa_ms(5);
    }
    VERIFICA(trovato);
    VERIFICA(ph_componente_vivo(bn.c));
    PhVideoRichiesta r = richiesta("com.a");
    PhVideoFlusso *f = ph_video_apri(bn.c, &r, &e);
    VERIFICA(f != NULL);
    VERIFICA(ph_video_chiave(f, &e) == 0);
    ph_video_chiudi(f, false);
    banco_fine(&bn);
}

int main(void)
{
    prova_avvio_e_chiusura();
    prova_nomi_diversi();
    prova_errori_di_avvio();
    prova_video();
    prova_richieste_ed_errori();
    prova_eventi();
    prova_input_byte();
    prova_input_concorrente();
    prova_richieste_concorrenti();
    prova_risposte_rotte();
    prova_caduta();
    prova_chiusura_sblocca_lettura();
    prova_video_malformato();
    prova_spazzatura();
    prova_pacchetto_grande();
    prova_pausa_e_ripresa();
    prova_presenza_dopo_silenzio();
    prova_presenza_con_traffico();
    prova_presenza_chiusura_durante_attesa();
    prova_presenza_chiusura_durante_presenza_lenta();
    prova_presenza_risposte_cattive();
    prova_presenza_non_dopo_chiudi();
    prova_presenza_dopo_sospensione();
    prova_chiusura_con_richiesta_ferma();
    prova_richieste_su_flusso_in_chiusura();
    prova_chiusura_aspetta_richiesta_in_corso();
    prova_richiesta_generica();
    prova_ascoltatori();
    prova_ascoltatore_tolto();
    prova_canale_audio();
    prova_chiusura_in_parallelo();
    printf("componente: tutte le prove riuscite\n");
    return 0;
}
