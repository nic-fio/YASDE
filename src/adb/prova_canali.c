/* Prove del client ADB contro il finto telefono: collegamento, non abbinato,
 * canali, thread insieme, controllo di flusso, shell v2, caduta della connessione. */
#include "adb/adb.h"
#include "finto_telefono.h"

#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>
#include <unistd.h>

static PhChiave *chiave;
/* Tutte le prove girano due volte: telefono senza e con la caratteristica delayed_ack. */
static bool g_delayed;

static int64_t ora_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (int64_t)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

static PhAdb *collega(FintoTelefono **ft, FtModo modo, bool *non_abbinato)
{
    uint16_t porta = 0;
    *ft = ft_avvia_cfg(chiave, modo, FT_MAX_DATI, g_delayed, &porta);
    assert(*ft != NULL);
    PhErrore e;
    PhAdb *a = ph_adb_collega("127.0.0.1", porta, chiave, 10000, non_abbinato, &e);
    if (modo == FT_NORMALE && a == NULL)
        fprintf(stderr, "collegamento fallito: %s\n", e.testo);
    return a;
}

static void aspetta_che(int (*f)(FintoTelefono *), FintoTelefono *ft, int valore)
{
    for (int i = 0; i < 200 && f(ft) < valore; i++)
        usleep(10 * 1000);
    assert(f(ft) >= valore);
}

static void prova_collegamento(void)
{
    FintoTelefono *ft;
    bool na = true;
    PhAdb *a = collega(&ft, FT_NORMALE, &na);
    assert(a != NULL && !na);
    assert(ph_adb_viva(a));
    assert(strstr(ph_adb_presentazione(a), "ro.product.model=FINTO") != NULL);
    assert((strstr(ph_adb_presentazione(a), "delayed_ack") != NULL) == g_delayed);
    assert(ft_certificato_client_ricevuto(ft)); /* il TLS e' stato fatto con il certificato del PC */
    ph_adb_chiudi(a);
    ft_ferma(ft);
}

static void prova_non_abbinato(void)
{
    const FtModo modi[] = {FT_CHIUDE_DOPO_TLS, FT_AUTH_DOPO_TLS, FT_AUTH_SUBITO};
    for (size_t i = 0; i < 3; i++) {
        FintoTelefono *ft;
        bool na = false;
        PhAdb *a = collega(&ft, modi[i], &na);
        assert(a == NULL && na);
        ft_ferma(ft);
    }
    /* porta chiusa: errore, ma non "non abbinato" */
    int s = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in sa = {0};
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    socklen_t l = sizeof sa;
    assert(bind(s, (struct sockaddr *)&sa, sizeof sa) == 0);
    assert(getsockname(s, (struct sockaddr *)&sa, &l) == 0);
    close(s);
    bool na = true;
    PhErrore e;
    assert(ph_adb_collega("127.0.0.1", ntohs(sa.sin_port), chiave, 2000, &na, &e) == NULL);
    assert(!na && e.testo[0] != '\0');
    /* telefono che non risponde: scade il tempo */
    s = socket(AF_INET, SOCK_STREAM, 0);
    sa.sin_port = 0;
    assert(bind(s, (struct sockaddr *)&sa, sizeof sa) == 0 && listen(s, 1) == 0);
    assert(getsockname(s, (struct sockaddr *)&sa, &l) == 0);
    int64_t t0 = ora_ms();
    assert(ph_adb_collega("127.0.0.1", ntohs(sa.sin_port), chiave, 300, &na, &e) == NULL);
    assert(!na && ora_ms() - t0 < 3000);
    close(s);
}

static void prova_canali_semplici(void)
{
    FintoTelefono *ft;
    PhAdb *a = collega(&ft, FT_NORMALE, NULL);
    assert(a != NULL);
    PhErrore e;

    assert(ph_canale_apri(a, "rifiuta:", 2000, &e) == NULL);
    assert(strstr(e.testo, "rifiutato") != NULL);
    assert(ph_adb_viva(a));

    PhCanale *c = ph_canale_apri(a, "chiudi:", 2000, &e);
    assert(c != NULL);
    char buf[16];
    assert(ph_canale_leggi_tutto(c, buf, 3, 2000, &e) == 0 && memcmp(buf, "abc", 3) == 0);
    assert(ph_canale_leggi(c, buf, sizeof buf, 2000, &e) == PH_CANALE_CHIUSO);
    assert(ph_canale_scrivi(c, "x", 1, &e) == -1);
    ph_canale_chiudi(c);

    /* dati ricevuti in piu' pezzi, letti a piccole porzioni */
    c = ph_canale_apri(a, "grande:", 2000, &e);
    assert(c != NULL);
    uint8_t *g = malloc(300000);
    assert(g != NULL);
    size_t tot = 0;
    while (tot < 300000) {
        size_t k = 1 + (tot * 7919) % 70000;
        if (k > 300000 - tot)
            k = 300000 - tot;
        assert(ph_canale_leggi_tutto(c, g + tot, k, 5000, &e) == 0);
        tot += k;
    }
    for (size_t i = 0; i < 300000; i++)
        assert(g[i] == ft_pattern(2, i));
    free(g);
    /* i WRTE ricevuti sono stati confermati con OKAY */
    if (!g_delayed) /* con delayed_ack l'OKAY e' cumulativo e parte a blocchi */
        aspetta_che(ft_okay_ricevuti, ft, 5);
    ph_canale_chiudi(c);

    /* timeout in lettura: TEMPO, non errore */
    c = ph_canale_apri(a, "silenzio:", 2000, &e);
    assert(c != NULL);
    int64_t t0 = ora_ms();
    assert(ph_canale_leggi(c, buf, sizeof buf, 150, &e) == PH_CANALE_TEMPO);
    assert(ora_ms() - t0 >= 140);
    assert(ph_canale_leggi(c, buf, sizeof buf, 0, &e) == PH_CANALE_TEMPO);
    assert(ph_canale_leggi_tutto(c, buf, 4, 50, &e) == -1);
    /* chiusura pulita: il telefono riceve CLSE e la connessione resta in vita */
    int prima = ft_clse_ricevuti(ft);
    ph_canale_chiudi(c);
    aspetta_che(ft_clse_ricevuti, ft, prima + 1);
    assert(ph_adb_viva(a));

    PhCanale *eco = ph_canale_apri(a, "eco:", 2000, &e);
    assert(eco != NULL);
    assert(ph_canale_scrivi(eco, "ping", 4, &e) == 0);
    assert(ph_canale_leggi_tutto(eco, buf, 4, 2000, &e) == 0 && memcmp(buf, "ping", 4) == 0);
    ph_canale_chiudi(eco);

    ph_adb_chiudi(a);
    ft_ferma(ft);
}

static void prova_eco_grande(void)
{
    FintoTelefono *ft;
    PhAdb *a = collega(&ft, FT_NORMALE, NULL);
    assert(a != NULL);
    PhErrore e;
    PhCanale *c = ph_canale_apri(a, "eco:", 2000, &e);
    assert(c != NULL);
    /* dimensioni attorno ai limiti dei pacchetti: 1, un pacchetto, un pacchetto+1, piu' pacchetti */
    const size_t misure[] = {1, 4096, 4097, FT_MAX_DATI, FT_MAX_DATI + 1, 300001};
    for (size_t m = 0; m < sizeof misure / sizeof misure[0]; m++) {
        size_t n = misure[m];
        uint8_t *w = malloc(n), *r = malloc(n);
        assert(w && r);
        for (size_t i = 0; i < n; i++)
            w[i] = ft_pattern((uint32_t)m, i);
        assert(ph_canale_scrivi(c, w, n, &e) == 0);
        assert(ph_canale_leggi_tutto(c, r, n, 5000, &e) == 0);
        assert(memcmp(w, r, n) == 0);
        free(w);
        free(r);
    }
    ph_canale_chiudi(c);
    assert(ft_violazioni_flusso(ft) == 0);
    ph_adb_chiudi(a);
    ft_ferma(ft);
}

/* ---- Thread insieme sulla stessa connessione ---------------------------- */
typedef struct {
    PhAdb *a;
    int numero;
    int ok;
} ArgEco;

static void *lavoro_eco(void *p)
{
    ArgEco *arg = p;
    PhErrore e;
    PhCanale *c = ph_canale_apri(arg->a, "eco:", 5000, &e);
    assert(c != NULL);
    unsigned seme = (unsigned)arg->numero * 1000u;
    for (int giro = 0; giro < 40; giro++) {
        size_t n = 1 + ((size_t)rand_r(&seme) % 50000);
        uint8_t *w = malloc(n), *r = malloc(n);
        assert(w && r);
        for (size_t i = 0; i < n; i++)
            w[i] = ft_pattern((uint32_t)(arg->numero * 100 + giro), i);
        assert(ph_canale_scrivi(c, w, n, &e) == 0);
        assert(ph_canale_leggi_tutto(c, r, n, 10000, &e) == 0);
        assert(memcmp(w, r, n) == 0);
        free(w);
        free(r);
    }
    ph_canale_chiudi(c);
    arg->ok = 1;
    return NULL;
}

static void *lavoro_shell(void *p)
{
    ArgEco *arg = p;
    PhErrore e;
    for (int i = 0; i < 20; i++) {
        PhBuffer out = PH_BUFFER_VUOTO, err = PH_BUFFER_VUOTO;
        int codice = -1;
        assert(ph_shell_esegui(arg->a, "prova", &out, &err, &codice, 10000, &e) == 0);
        assert(codice == 7);
        assert(out.lunghezza == 5 && memcmp(out.dati, "ciao\n", 5) == 0);
        assert(err.lunghezza == 7 && memcmp(err.dati, "errore\n", 7) == 0);
        ph_buffer_libera(&out);
        ph_buffer_libera(&err);
    }
    arg->ok = 1;
    return NULL;
}

static void prova_thread_insieme(void)
{
    FintoTelefono *ft;
    PhAdb *a = collega(&ft, FT_NORMALE, NULL);
    assert(a != NULL);
    enum { ECHI = 6, SHELL = 3 };
    pthread_t th[ECHI + SHELL];
    ArgEco arg[ECHI + SHELL];
    for (int i = 0; i < ECHI + SHELL; i++) {
        arg[i] = (ArgEco){a, i + 1, 0};
        assert(pthread_create(&th[i], NULL, i < ECHI ? lavoro_eco : lavoro_shell, &arg[i]) == 0);
    }
    for (int i = 0; i < ECHI + SHELL; i++) {
        pthread_join(th[i], NULL);
        assert(arg[i].ok == 1);
    }
    assert(ph_adb_viva(a));
    /* tutti i WRTE del telefono sono stati confermati dal client */
    if (!g_delayed)
        aspetta_che(ft_okay_ricevuti, ft, ft_wrte_inviati(ft));
    assert(ft_violazioni_flusso(ft) == 0);
    ph_adb_chiudi(a);
    ft_ferma(ft);
}

/* delayed_ack: finestra rispettata e piu' pacchetti in volo senza OKAY per ciascuno. */
static void prova_finestra(void)
{
    FintoTelefono *ft;
    PhAdb *a = collega(&ft, FT_NORMALE, NULL);
    assert(a != NULL);
    PhErrore e;
    PhCanale *c = ph_canale_apri(a, "lento:", 2000, &e);
    assert(c != NULL);
    char dati[100] = {0};
    int64_t t0 = ora_ms();
    /* tre WRTE piccoli stanno nella finestra: nessuno aspetta i 100 ms del telefono */
    for (int i = 0; i < 3; i++)
        assert(ph_canale_scrivi(c, dati, sizeof dati, &e) == 0);
    assert(ora_ms() - t0 < 90);
    /* 600000 byte = piu' di due finestre: si ferma sulla finestra, riparte alla conferma */
    enum { GRANDE = 600000 };
    uint8_t *g = calloc(1, GRANDE);
    assert(g != NULL);
    t0 = ora_ms();
    assert(ph_canale_scrivi(c, g, GRANDE, &e) == 0);
    assert(ora_ms() - t0 >= 190);
    free(g);
    ph_canale_chiudi(c);
    aspetta_che(ft_clse_ricevuti, ft, 1);
    assert(ft_byte_lento(ft) == 300 + GRANDE);
    assert(ft_max_in_volo(ft) <= (long)FT_FINESTRA);           /* mai oltre la finestra */
    assert(ft_max_in_volo(ft) >= 2 * (long)FT_MAX_DATI);       /* piu' pacchetti insieme in volo */
    assert(ft_violazioni_flusso(ft) == 0);
    ph_adb_chiudi(a);
    ft_ferma(ft);
}

/* Un telefono che manda oltre la finestra concessa viola il protocollo: la connessione cade. */
static void prova_violazione_finestra(void)
{
    FintoTelefono *ft;
    PhAdb *a = collega(&ft, FT_NORMALE, NULL);
    assert(a != NULL);
    PhErrore e;
    PhCanale *c = ph_canale_apri(a, "fuori:12582912", 2000, &e); /* 12 MiB, nessuno legge */
    assert(c != NULL);
    for (int i = 0; i < 300 && ph_adb_viva(a); i++)
        usleep(10 * 1000);
    assert(!ph_adb_viva(a));
    char b[4];
    for (;;) { /* i dati gia' in coda si leggono, poi arriva l'errore */
        ssize_t r = ph_canale_leggi(c, b, sizeof b, 1000, &e);
        assert(r != PH_CANALE_TEMPO && r != PH_CANALE_CHIUSO);
        if (r < 0)
            break;
    }
    ph_canale_chiudi(c);
    ph_adb_chiudi(a);
    ft_ferma(ft);
}

static void prova_flusso(void)
{
    if (g_delayed) {
        prova_finestra();
        return;
    }
    FintoTelefono *ft;
    PhAdb *a = collega(&ft, FT_NORMALE, NULL);
    assert(a != NULL);
    PhErrore e;
    PhCanale *c = ph_canale_apri(a, "lento:", 2000, &e);
    assert(c != NULL);
    char dati[100] = {0};
    int64_t t0 = ora_ms();
    /* il 2o e il 3o WRTE aspettano l'OKAY del precedente: ognuno 100 ms */
    for (int i = 0; i < 3; i++)
        assert(ph_canale_scrivi(c, dati, sizeof dati, &e) == 0);
    assert(ora_ms() - t0 >= 190);
    /* una scrittura grande viene spezzata in pacchetti da non piu' di FT_MAX_DATI */
    uint8_t *g = calloc(1, 3 * FT_MAX_DATI + 10);
    assert(g != NULL);
    assert(ph_canale_scrivi(c, g, 3 * FT_MAX_DATI + 10, &e) == 0);
    free(g);
    ph_canale_chiudi(c); /* attende anche l'ultimo invio */
    aspetta_che(ft_clse_ricevuti, ft, 1);
    assert(ft_byte_lento(ft) == 300 + 3 * (long)FT_MAX_DATI + 10);
    assert(ft_violazioni_flusso(ft) == 0);
    ph_adb_chiudi(a);
    ft_ferma(ft);
}

static void prova_shell(void)
{
    FintoTelefono *ft;
    PhAdb *a = collega(&ft, FT_NORMALE, NULL);
    assert(a != NULL);
    PhErrore e;
    PhBuffer out = PH_BUFFER_VUOTO, err = PH_BUFFER_VUOTO;
    int codice = -1;

    assert(ph_shell_esegui(a, "prova", &out, &err, &codice, 5000, &e) == 0);
    assert(codice == 7 && out.lunghezza == 5 && err.lunghezza == 7);
    /* uscita diversa da zero non e' un errore della funzione; NULL ammessi */
    assert(ph_shell_esegui(a, "muto", NULL, NULL, NULL, 5000, &e) == 0);
    codice = -1;
    assert(ph_shell_esegui(a, "muto", NULL, NULL, &codice, 5000, &e) == 0 && codice == 0);

    ph_buffer_svuota(&out);
    assert(ph_shell_esegui(a, "grosso", &out, NULL, &codice, 5000, &e) == 0);
    assert(out.lunghezza == 100000 && codice == 0);
    for (size_t i = 0; i < 100000; i++)
        assert(out.dati[i] == ft_pattern(1, i));

    /* comando che non finisce: tempo scaduto, errore, connessione ancora buona */
    int64_t t0 = ora_ms();
    assert(ph_shell_esegui(a, "pendi", NULL, NULL, &codice, 200, &e) == -1);
    assert(ora_ms() - t0 >= 190 && ora_ms() - t0 < 3000);
    assert(strstr(e.testo, "tempo") != NULL);
    assert(ph_adb_viva(a));
    assert(ph_shell_esegui(a, "prova", NULL, NULL, &codice, 5000, &e) == 0 && codice == 7);

    ph_buffer_libera(&out);
    ph_buffer_libera(&err);
    ph_adb_chiudi(a);
    ft_ferma(ft);
}

/* ---- Caduta della connessione con letture in corso ---------------------- */
typedef struct {
    PhCanale *c;
    PhAdb *a;
    int tipo; /* 0 leggi infinito, 1 leggi_tutto, 2 shell che non finisce, 3 scrittura */
    ssize_t esito;
} ArgCaduta;

static void *lavoro_caduta(void *p)
{
    ArgCaduta *arg = p;
    PhErrore e;
    char buf[32];
    switch (arg->tipo) {
    case 0:
        arg->esito = ph_canale_leggi(arg->c, buf, sizeof buf, -1, &e);
        break;
    case 1:
        arg->esito = ph_canale_leggi_tutto(arg->c, buf, sizeof buf, -1, &e);
        break;
    case 2:
        arg->esito = ph_shell_esegui(arg->a, "pendi", NULL, NULL, NULL, -1, &e);
        break;
    default: {
        /* il primo WRTE parte, il secondo resta in attesa dell'OKAY che non arrivera' */
        /* piu' della finestra (o piu' di un pacchetto, senza delayed_ack): si ferma
         * in attesa di un OKAY che non arrivera' */
        enum { TANTO = FT_FINESTRA + 1000 };
        uint8_t *g = calloc(1, TANTO);
        assert(g != NULL);
        arg->esito = ph_canale_scrivi(arg->c, g, TANTO, &e);
        free(g);
        break;
    }
    }
    return NULL;
}

static void prova_caduta(int chiudi_noi)
{
    FintoTelefono *ft;
    PhAdb *a = collega(&ft, FT_NORMALE, NULL);
    assert(a != NULL);
    PhErrore e;
    enum { N = 5 };
    ArgCaduta arg[N];
    pthread_t th[N];
    for (int i = 0; i < N; i++) {
        arg[i] = (ArgCaduta){NULL, a, i < 2 ? i : (i == 2 ? 1 : (i == 3 ? 2 : 3)), 99};
        if (arg[i].tipo != 2) {
            arg[i].c = ph_canale_apri(a, arg[i].tipo == 3 ? "nookay:" : "silenzio:", 2000, &e);
            assert(arg[i].c != NULL);
        }
        assert(pthread_create(&th[i], NULL, lavoro_caduta, &arg[i]) == 0);
    }
    usleep(150 * 1000); /* i thread sono ormai bloccati in lettura / in attesa dell'OKAY */
    if (chiudi_noi)
        ph_adb_chiudi(a);
    else
        ft_cadi(ft);
    for (int i = 0; i < N; i++) {
        pthread_join(th[i], NULL); /* se un thread restasse bloccato il test scade */
        assert(arg[i].esito == PH_CANALE_ERRORE || arg[i].esito == -1);
    }
    if (!chiudi_noi) {
        for (int i = 0; i < 200 && ph_adb_viva(a); i++)
            usleep(10 * 1000);
        assert(!ph_adb_viva(a));
        assert(ph_canale_apri(a, "eco:", 1000, &e) == NULL);
    }
    for (int i = 0; i < N; i++) {
        if (arg[i].c != NULL) {
            char b[4];
            assert(ph_canale_scrivi(arg[i].c, "x", 1, &e) == -1);
            assert(ph_canale_leggi(arg[i].c, b, sizeof b, 100, &e) == PH_CANALE_ERRORE);
            ph_canale_chiudi(arg[i].c);
        }
    }
    if (!chiudi_noi)
        ph_adb_chiudi(a);
    ft_ferma(ft);
}

/* ph_canale_chiudi da un thread sblocca la lettura in corso in un altro */
static void *lettura_bloccata(void *p)
{
    PhCanale *c = p;
    PhErrore e;
    char b[4];
    ssize_t r = ph_canale_leggi(c, b, sizeof b, -1, &e);
    assert(r == PH_CANALE_ERRORE);
    return NULL;
}

static void prova_chiudi_con_lettura(void)
{
    FintoTelefono *ft;
    PhAdb *a = collega(&ft, FT_NORMALE, NULL);
    assert(a != NULL);
    PhErrore e;
    PhCanale *c = ph_canale_apri(a, "silenzio:", 2000, &e);
    assert(c != NULL);
    pthread_t t;
    assert(pthread_create(&t, NULL, lettura_bloccata, c) == 0);
    usleep(100 * 1000);
    ph_canale_chiudi(c); /* torna solo dopo che il lettore e' uscito */
    pthread_join(t, NULL);
    assert(ph_adb_viva(a));
    ph_adb_chiudi(a);
    ft_ferma(ft);
}

/* Dati ostili a connessione avviata. */
static void prova_ostile(void)
{
    PhErrore e;
    char b[8];
    /* 0: id inesistenti o sbagliati: ignorati, la connessione e il canale restano buoni */
    {
        FintoTelefono *ft;
        PhAdb *a = collega(&ft, FT_NORMALE, NULL);
        assert(a != NULL);
        PhCanale *c = ph_canale_apri(a, "ostile:0", 2000, &e);
        assert(c != NULL);
        /* il client risponde CLSE a WRTE/OKAY di canali che non esistono */
        aspetta_che(ft_clse_ricevuti, ft, 2);
        assert(ph_canale_leggi(c, b, sizeof b, 200, &e) == PH_CANALE_TEMPO); /* nessun dato falso in coda */
        assert(ph_adb_viva(a));
        PhCanale *eco = ph_canale_apri(a, "eco:", 2000, &e);
        assert(eco != NULL);
        assert(ph_canale_scrivi(eco, "ok", 2, &e) == 0);
        assert(ph_canale_leggi_tutto(eco, b, 2, 2000, &e) == 0 && memcmp(b, "ok", 2) == 0);
        ph_canale_chiudi(eco);
        ph_canale_chiudi(c);
        ph_adb_chiudi(a);
        ft_ferma(ft);
    }
    /* 1 e 2: magic errato, lunghezza oltre il massimo: la connessione cade, nessun thread resta bloccato */
    for (int scenario = 1; scenario <= 2; scenario++) {
        FintoTelefono *ft;
        PhAdb *a = collega(&ft, FT_NORMALE, NULL);
        assert(a != NULL);
        char serv[16];
        snprintf(serv, sizeof serv, "ostile:%d", scenario);
        PhCanale *c = ph_canale_apri(a, serv, 2000, &e);
        assert(c != NULL);
        assert(ph_canale_leggi(c, b, sizeof b, 3000, &e) == PH_CANALE_ERRORE);
        assert(!ph_adb_viva(a));
        ph_canale_chiudi(c);
        ph_adb_chiudi(a);
        ft_ferma(ft);
    }
    /* pacchetto shell v2 con lunghezza falsa: errore, non attesa infinita */
    {
        FintoTelefono *ft;
        PhAdb *a = collega(&ft, FT_NORMALE, NULL);
        assert(a != NULL);
        int64_t t0 = ora_ms();
        assert(ph_shell_esegui(a, "falso", NULL, NULL, NULL, 5000, &e) == -1);
        assert(ora_ms() - t0 < 4000);
        assert(ph_adb_viva(a));
        ph_adb_chiudi(a);
        ft_ferma(ft);
    }
}

/* OKAY malformati con delayed_ack: violazione del protocollo, la connessione cade. */
static void prova_okay_malformato(void)
{
    PhErrore e;
    char b[4];
    for (int scenario = 3; scenario <= 4; scenario++) {
        FintoTelefono *ft;
        PhAdb *a = collega(&ft, FT_NORMALE, NULL);
        assert(a != NULL);
        char serv[16];
        snprintf(serv, sizeof serv, "ostile:%d", scenario);
        PhCanale *c = ph_canale_apri(a, serv, 2000, &e);
        assert(c != NULL);
        assert(ph_canale_leggi(c, b, sizeof b, 3000, &e) == PH_CANALE_ERRORE);
        assert(!ph_adb_viva(a));
        ph_canale_chiudi(c);
        ph_adb_chiudi(a);
        ft_ferma(ft);
    }
    /* OKAY di apertura senza carico: errore chiaro subito, non 30 s dopo */
    FintoTelefono *ft;
    PhAdb *a = collega(&ft, FT_NORMALE, NULL);
    assert(a != NULL);
    int64_t t0 = ora_ms();
    assert(ph_canale_apri(a, "apertura0:", 5000, &e) == NULL);
    assert(ora_ms() - t0 < 3000 && e.testo[0] != '\0');
    assert(!ph_adb_viva(a));
    ph_adb_chiudi(a);
    ft_ferma(ft);
}

/* "features=delayed_ack" dentro il campo del modello non e' la lista delle caratteristiche. */
static void prova_banner_ingannevole(void)
{
    FintoTelefono *ft;
    PhAdb *a = collega(&ft, FT_BANNER_INGANNO, NULL);
    assert(a != NULL);
    PhErrore e;
    PhCanale *c = ph_canale_apri(a, "eco:", 2000, &e); /* con arg1 != 0 il finto telefono segnerebbe una violazione */
    assert(c != NULL);
    assert(ph_canale_scrivi(c, "ok", 2, &e) == 0);
    char b[2];
    assert(ph_canale_leggi_tutto(c, b, 2, 2000, &e) == 0);
    assert(ft_violazioni_flusso(ft) == 0);
    ph_canale_chiudi(c);
    ph_adb_chiudi(a);
    ft_ferma(ft);
}

/* Risposta tardiva a un'apertura scaduta: il client chiude il servizio sul telefono. */
static void prova_apertura_tardiva(void)
{
    FintoTelefono *ft;
    PhAdb *a = collega(&ft, FT_NORMALE, NULL);
    assert(a != NULL);
    PhErrore e;
    assert(ph_canale_apri(a, "tardivo:", 100, &e) == NULL);
    assert(strstr(e.testo, "tempo") != NULL);
    aspetta_che(ft_clse_ricevuti, ft, 1); /* arriva dopo l'OKAY tardivo */
    assert(ph_adb_viva(a));
    /* servizio con nome piu' lungo del massimo: errore pulito, senza invio */
    char *lungo = malloc(FT_MAX_DATI + 10);
    assert(lungo != NULL);
    memset(lungo, 'a', FT_MAX_DATI + 9);
    lungo[FT_MAX_DATI + 9] = '\0';
    assert(ph_canale_apri(a, lungo, 1000, &e) == NULL);
    free(lungo);
    assert(ph_adb_viva(a));
    ph_adb_chiudi(a);
    ft_ferma(ft);
}

/* Un lettore lento frena il telefono: la coda resta limitata, poi tutto arriva intatto. */
static void prova_freno(void)
{
    /* senza delayed_ack il freno e' la soglia di 2 MiB; con delayed_ack la finestra di 4 MiB */
    enum { TOT = 16 * 1024 * 1024 };
    const long SOGLIA = g_delayed ? 4 * 1024 * 1024 : 2 * 1024 * 1024;
    FintoTelefono *ft;
    PhAdb *a = collega(&ft, FT_NORMALE, NULL);
    assert(a != NULL);
    PhErrore e;
    char serv[32];
    snprintf(serv, sizeof serv, "sorgente:%d", TOT);
    PhCanale *c = ph_canale_apri(a, serv, 2000, &e);
    assert(c != NULL);
    usleep(600 * 1000); /* nessuno legge: il telefono si deve fermare vicino alla soglia */
    long spediti = ft_byte_sorgente(ft);
    assert(spediti >= SOGLIA - (long)FT_MAX_DATI && spediti <= SOGLIA + 2 * (long)FT_MAX_DATI);
    uint8_t *buf = malloc(100000);
    assert(buf != NULL);
    size_t tot = 0;
    while (tot < TOT) {
        size_t k = TOT - tot < 100000 ? TOT - tot : 100000;
        assert(ph_canale_leggi_tutto(c, buf, k, 10000, &e) == 0);
        for (size_t i = 0; i < k; i++)
            assert(buf[i] == ft_pattern(3, tot + i));
        tot += k;
    }
    assert(ft_byte_sorgente(ft) == TOT);
    assert(ph_adb_viva(a));
    free(buf);
    ph_canale_chiudi(c);
    ph_adb_chiudi(a);
    ft_ferma(ft);
}

int main(void)
{
    char dir[] = "prova-canali-XXXXXX";
    assert(mkdtemp(dir) != NULL);
    char file[64];
    snprintf(file, sizeof file, "%s/chiave", dir);
    PhErrore e;
    chiave = ph_chiave_carica_o_crea(file, &e);
    assert(chiave != NULL);

    for (g_delayed = false;; g_delayed = true) {
        prova_collegamento();
        prova_non_abbinato();
        prova_canali_semplici();
        prova_eco_grande();
        prova_thread_insieme();
        prova_flusso();
        prova_shell();
        prova_caduta(0);
        prova_caduta(1);
        prova_chiudi_con_lettura();
        prova_ostile();
        prova_apertura_tardiva();
        prova_freno();
        if (g_delayed) {
            prova_violazione_finestra();
            prova_okay_malformato();
        }
        prova_banner_ingannevole();
        if (g_delayed)
            break;
    }

    ph_chiave_libera(chiave);
    unlink(file);
    rmdir(dir);
    return 0;
}
