/* Prove del collegamento con telefoni e rete finti (finto.c). Tempi piccoli
 * (decine di millisecondi): niente telefono vero, niente rete. */
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "collegamento/collegamento.h"
#include "finto.h"

static int errori;
#define CHECK(c)                                                              \
    do {                                                                      \
        if (!(c)) {                                                           \
            printf("FALLITO riga %d: %s\n", __LINE__, #c);                    \
            fflush(stdout);                                                   \
            errori++;                                                         \
        }                                                                     \
    } while (0)

static long ora_ms(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (long)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}
static void dormi(int ms) {
    struct timespec t = {ms / 1000, (long)(ms % 1000) * 1000000L};
    nanosleep(&t, NULL);
}

/* ---- Registro degli avvisi ------------------------------------------------------*/
#define MAX_EV 512
static pthread_mutex_t Mev = PTHREAD_MUTEX_INITIALIZER;
static PhColAvviso ev[MAX_EV];
static int ev_n;
static atomic_int perdite_n;
static void (*hook_avviso)(const PhColAvviso *);
static void (*hook_perdita)(void);

static void su_avviso(const PhColAvviso *a, void *u) {
    (void)u;
    pthread_mutex_lock(&Mev);
    if (ev_n < MAX_EV) ev[ev_n++] = *a;
    pthread_mutex_unlock(&Mev);
    if (hook_avviso) hook_avviso(a);
}
static void su_perdita(void *u) {
    (void)u;
    atomic_fetch_add(&perdite_n, 1);
    if (hook_perdita) hook_perdita();
}
static int ev_conta_stato(PhColStato st) {
    int k = 0;
    pthread_mutex_lock(&Mev);
    for (int i = 0; i < ev_n; i++)
        if (ev[i].stato == st) k++;
    pthread_mutex_unlock(&Mev);
    return k;
}
static PhColAvviso ev_ultimo(void) {
    pthread_mutex_lock(&Mev);
    PhColAvviso a = ev[ev_n > 0 ? ev_n - 1 : 0];
    pthread_mutex_unlock(&Mev);
    return a;
}
/* Sequenza degli stati con una lettera, senza ripetizioni consecutive. */
static void ev_seq(char *out, size_t cap) {
    static const char lettere[] = "CKBPXN";
    size_t j = 0;
    pthread_mutex_lock(&Mev);
    for (int i = 0; i < ev_n && j + 1 < cap; i++) {
        char l = lettere[ev[i].stato];
        if (j == 0 || out[j - 1] != l) out[j++] = l;
    }
    pthread_mutex_unlock(&Mev);
    out[j] = '\0';
}

static PhCollegamento *avvia(const PhColOpzioni *o) {
    pthread_mutex_lock(&Mev);
    ev_n = 0;
    pthread_mutex_unlock(&Mev);
    atomic_store(&perdite_n, 0);
    PhErrore e = {{0}};
    PhCollegamento *c = ph_col_avvia(o, &e);
    CHECK(c != NULL);
    return c;
}

/* Opzioni veloci; i singoli test cambiano qualcosa. */
static PhColOpzioni veloci(void) {
    PhColOpzioni o;
    ph_col_opzioni_predefinite(&o);
    o.jar_locale = "jar-finto";
    o.avviso = su_avviso;
    o.in_perdita = su_perdita;
    o.durata_scoperta_ms = 10;
    o.timeout_collega_ms = 100;
    o.timeout_componente_ms = 100;
    o.timeout_abbinamento_ms = 100;
    o.pausa_cerca_ms = 10;
    o.riprova_iniziale_ms = 20;
    o.riprova_massimo_ms = 80;
    o.controllo_ms = 5;
    o.attesa_prestiti_ms = 100;
    return o;
}

static bool attendi_stato(PhCollegamento *c, PhColStato st, int ms) {
    long fine = ora_ms() + ms;
    for (;;) {
        PhColAvviso a;
        ph_col_stato(c, &a);
        if (a.stato == st) return true;
        if (ora_ms() > fine) return false;
        dormi(2);
    }
}
static bool attendi_collega(int n, int ms) {
    long fine = ora_ms() + ms;
    while (finto_collega_chiamate() < n) {
        if (ora_ms() > fine) return false;
        dormi(2);
    }
    return true;
}
/* Nessun motivo registrato deve contenere indirizzi o seriali (anche se gli
 * errori dei pezzi sotto li riportano: il finto lo fa apposta). */
static void motivi_puliti(void) {
    pthread_mutex_lock(&Mev);
    for (int i = 0; i < ev_n; i++) {
        const char *m = ev[i].motivo;
        if (strstr(m, "10.") || strstr(m, "fe80") || strstr(m, "R5CT") || strstr(m, "SER0000") ||
            strstr(m, ":5555") || strstr(m, "ab_cd")) {
            printf("motivo con dati personali: %s\n", m);
            errori++;
        }
    }
    pthread_mutex_unlock(&Mev);
}
static void nessun_oggetto_vivo(void) {
    motivi_puliti();
    CHECK(finto_vivi_adb() == 0);
    CHECK(finto_vivi_componenti() == 0);
    CHECK(finto_vivi_dati() == 0);
    CHECK(finto_vivi_chiavi() == 0);
}
static void un_telefono(void) {
    finto_reset();
    finto_tel(0, "10.0.0.1", 5555, "R5CT0000000", "Modello-X");
    int l[] = {0};
    finto_scoperta_telefoni(l, 1);
}

static void mondo_tre_nome(void) {
    finto_reset();
    finto_tel(0, "10.0.0.1", 5555, "SER0000AAA", "Mod-A");
    finto_tel(1, "10.0.0.2", 5555, "SER0000BBB", "Mod-B");
}

/* ---- Prove -----------------------------------------------------------------------*/
static void prova_prima_connessione(void) {
    puts("prima connessione");
    un_telefono();
    PhColOpzioni o = veloci();
    PhCollegamento *c = avvia(&o);
    CHECK(attendi_stato(c, PH_COL_COLLEGATO, 3000));
    PhColAvviso a;
    ph_col_stato(c, &a);
    CHECK(a.ha_telefono);
    CHECK(strcmp(a.telefono.seriale, "R5CT0000000") == 0);
    CHECK(strcmp(a.telefono.nome, "Modello-X") == 0);
    CHECK(a.telefono.porta == 5555 && strcmp(a.telefono.indirizzo, "10.0.0.1") == 0);
    CHECK(a.telefono.info.api == 34 && a.telefono.info.larghezza == 1080);
    CHECK(!a.telefono.conosciuto);
    CHECK(a.telefoni_trovati == 1 && !a.scelta_arbitraria);
    char seq[32];
    ev_seq(seq, sizeof seq);
    CHECK(strcmp(seq, "CK") == 0);

    FintoNoto n;
    CHECK(finto_noti_cerca("R5CT0000000", &n));
    CHECK(strcmp(n.indirizzo, "10.0.0.1:5555") == 0 && strcmp(n.nome, "Modello-X") == 0);
    CHECK(n.visto > 1000000000L);

    PhErrore e = {{0}};
    PhColPrestito *p = ph_col_presta(c, &e);
    CHECK(p != NULL);
    if (p) {
        CHECK(ph_col_prestito_adb(p) && ph_col_prestito_componente(p) && ph_col_prestito_dati(p));
        CHECK(ph_col_prestito_attivo(p));
        CHECK(strcmp(ph_col_prestito_telefono(p)->seriale, "R5CT0000000") == 0);
        ph_col_restituisci(p);
    }
    CHECK(ph_col_chiudi(c) == 0);
    char ord[32];
    finto_ordine_rilascio(ord, sizeof ord);
    CHECK(strcmp(ord, "DCA") == 0);
    CHECK(ev_ultimo().stato == PH_COL_CHIUSO);
    CHECK(atomic_load(&perdite_n) == 1); /* anche la chiusura chiede di restituire */
    nessun_oggetto_vivo();
}

static void prova_non_trovato_poi_comparso(void) {
    puts("telefono non trovato e poi comparso");
    un_telefono();
    finto_tel_acceso(0, false);
    PhColOpzioni o = veloci();
    PhCollegamento *c = avvia(&o);
    long fine = ora_ms() + 3000;
    while (finto_scoperta_chiamate() < 4 && ora_ms() < fine) dormi(2);
    CHECK(finto_scoperta_chiamate() >= 4);
    PhColAvviso a;
    ph_col_stato(c, &a);
    CHECK(a.stato == PH_COL_CERCO && strstr(a.motivo, "nessun telefono"));
    CHECK(!a.ha_telefono);
    /* un motivo ripetuto non rifa' l'avviso: CERCO due volte al massimo (avvio + motivo) */
    CHECK(ev_conta_stato(PH_COL_CERCO) <= 2);
    PhErrore e = {{0}};
    CHECK(ph_col_presta(c, &e) == NULL);
    finto_tel_acceso(0, true);
    CHECK(attendi_stato(c, PH_COL_COLLEGATO, 3000));
    CHECK(ph_col_chiudi(c) == 0);
    nessun_oggetto_vivo();
}

static void prova_non_abbinato(void) {
    puts("PC non abbinato, poi abbinato");
    un_telefono();
    finto_tel_abbinato(0, false);
    finto_abbinamento_telefono(0);
    PhColOpzioni o = veloci();
    PhCollegamento *c = avvia(&o);
    CHECK(attendi_stato(c, PH_COL_NON_ABBINATO, 3000));
    int chiamate = finto_collega_chiamate();
    dormi(100);
    CHECK(finto_collega_chiamate() == chiamate); /* fermo: non e' un errore di rete */
    PhErrore e = {{0}};
    CHECK(ph_col_presta(c, &e) == NULL);

    /* argomenti non validi */
    CHECK(ph_col_abbina_a(c, "", 40000, "123456", NULL) == -1);
    CHECK(ph_col_abbina_a(c, "10.0.0.1", 0, "123456", NULL) == -1);
    CHECK(ph_col_abbina(c, "", NULL) == -1);

    /* il servizio di abbinamento non si vede: lo dice, senza provarci */
    finto_abbinamento_visibile(false);
    int n1 = ev_conta_stato(PH_COL_NON_ABBINATO);
    CHECK(ph_col_abbina(c, "123456", NULL) == 0);
    long fine1 = ora_ms() + 3000;
    while (ev_conta_stato(PH_COL_NON_ABBINATO) == n1 && ora_ms() < fine1) dormi(2);
    PhColAvviso av1;
    ph_col_stato(c, &av1);
    CHECK(av1.stato == PH_COL_NON_ABBINATO && strstr(av1.motivo, "Associa dispositivo"));
    CHECK(finto_abbinamento_chiamate() == 0);
    finto_abbinamento_visibile(true);

    /* codice sbagliato: resta fermo e lo dice, con un avviso nuovo */
    int n0 = ev_conta_stato(PH_COL_NON_ABBINATO);
    CHECK(ph_col_abbina(c, "000000", NULL) == 0);
    long fine = ora_ms() + 3000;
    while (ev_conta_stato(PH_COL_NON_ABBINATO) == n0 && ora_ms() < fine) dormi(2);
    CHECK(ev_conta_stato(PH_COL_NON_ABBINATO) == n0 + 1);
    CHECK(finto_abbinamento_chiamate() == 1);
    PhColAvviso a;
    ph_col_stato(c, &a);
    CHECK(a.stato == PH_COL_NON_ABBINATO && strstr(a.motivo, "non riuscito"));

    /* "riconnetti ora" riprova senza abbinare: un nuovo tentativo, stessa risposta */
    int coll = finto_collega_chiamate();
    CHECK(ph_col_riconnetti_ora(c, NULL) == 0);
    CHECK(attendi_collega(coll + 1, 3000));
    CHECK(attendi_stato(c, PH_COL_NON_ABBINATO, 3000));
    CHECK(ev_conta_stato(PH_COL_CERCO) >= 2);

    /* codice giusto: abbina e si collega da solo */
    CHECK(ph_col_abbina(c, "123456", NULL) == 0); /* la porta la trova il collegamento */
    CHECK(attendi_stato(c, PH_COL_COLLEGATO, 3000));
    CHECK(finto_abbinamento_chiamate() == 2);
    char seq[32];
    ev_seq(seq, sizeof seq);
    CHECK(strstr(seq, "NCK") != NULL);
    /* fuori da NON_ABBINATO non si abbina */
    CHECK(ph_col_abbina(c, "123456", NULL) == -1);
    CHECK(ph_col_chiudi(c) == 0);
    nessun_oggetto_vivo();

    puts("abbinamento con host e porta espliciti");
    un_telefono();
    finto_tel_abbinato(0, false);
    finto_abbinamento_telefono(0);
    finto_abbinamento_visibile(false); /* la scoperta non lo vede */
    c = avvia(&o);
    CHECK(attendi_stato(c, PH_COL_NON_ABBINATO, 3000));
    CHECK(ph_col_abbina_a(c, "10.0.0.1", 40000, "123456", NULL) == 0);
    CHECK(attendi_stato(c, PH_COL_COLLEGATO, 3000));
    CHECK(ph_col_chiudi(c) == 0);
    nessun_oggetto_vivo();

    puts("non abbinato all'ultimo indirizzo, ma un altro telefono risponde");
    mondo_tre_nome();
    finto_tel_abbinato(0, false);
    finto_noti_aggiungi("SER0000AAA", "Mod-A", "10.0.0.1:5555", 500);
    int l2[] = {0, 1};
    finto_scoperta_telefoni(l2, 2);
    c = avvia(&o);
    CHECK(attendi_stato(c, PH_COL_COLLEGATO, 3000));
    PhColAvviso av2;
    ph_col_stato(c, &av2);
    CHECK(strcmp(av2.telefono.seriale, "SER0000BBB") == 0);
    CHECK(ph_col_chiudi(c) == 0);
    nessun_oggetto_vivo();
}

static void prova_caduta_e_riconnessione(void) {
    puts("caduta e riconnessione automatica, intervalli crescenti");
    un_telefono();
    PhColOpzioni o = veloci();
    PhCollegamento *c = avvia(&o);
    CHECK(attendi_stato(c, PH_COL_COLLEGATO, 3000));
    int creati = finto_creati_adb();
    int prima = finto_collega_chiamate();
    finto_tel_acceso(0, false);
    CHECK(attendi_stato(c, PH_COL_PERSO, 3000));
    PhColAvviso a;
    ph_col_stato(c, &a);
    CHECK(a.motivo[0] != '\0' && !a.ha_telefono);
    CHECK(finto_vivi_adb() == 0 && finto_vivi_componenti() == 0 && finto_vivi_dati() == 0);
    CHECK(attendi_collega(prima + 10, 10000));
    long t[64];
    int n = finto_collega_istanti(t, 64);
    /* t[prima] e' il primo tentativo dopo la caduta (subito); poi attese 20, 40, 80, 80... */
    CHECK(n >= prima + 10);
    long attese[4] = {20, 40, 80, 80};
    for (int k = 0; k < 4 && n >= prima + 10; k++) {
        long gap = t[prima + 1 + k] - t[prima + k];
        CHECK(gap >= attese[k] - 3);
    }
    /* l'ottavo intervallo, senza tetto, sarebbe 2560 ms: il tetto regge */
    if (n >= prima + 10) CHECK(t[prima + 8] - t[prima + 7] < 2000);
    finto_tel_acceso(0, true);
    CHECK(attendi_stato(c, PH_COL_COLLEGATO, 3000));
    CHECK(finto_creati_adb() == creati + 1); /* una connessione nuova */
    CHECK(finto_vivi_adb() == 1);
    char seq[32];
    ev_seq(seq, sizeof seq);
    CHECK(strcmp(seq, "CKPK") == 0);
    CHECK(ph_col_chiudi(c) == 0);
    nessun_oggetto_vivo();
}

static void prova_riconnetti_ora(void) {
    puts("riconnetti ora");
    un_telefono();
    PhColOpzioni o = veloci();
    o.riprova_iniziale_ms = 5000;
    o.riprova_massimo_ms = 5000;
    PhCollegamento *c = avvia(&o);
    CHECK(attendi_stato(c, PH_COL_COLLEGATO, 3000));
    CHECK(ph_col_riconnetti_ora(c, NULL) == -1); /* collegato: niente da riprovare */
    int coll = finto_collega_chiamate();
    int fall = finto_collega_fallite();
    finto_tel_acceso(0, false);
    CHECK(attendi_stato(c, PH_COL_PERSO, 3000));
    long fine = ora_ms() + 3000; /* il tentativo immediato dopo la caduta e' fallito... */
    while (finto_collega_fallite() < fall + 1 && ora_ms() < fine) dormi(1);
    CHECK(finto_collega_fallite() >= fall + 1);
    finto_tel_acceso(0, true);   /* ...ora il telefono torna */
    dormi(100);
    PhColAvviso a;
    ph_col_stato(c, &a);
    CHECK(a.stato == PH_COL_PERSO); /* aspetta ancora i 5 secondi */
    long t0 = ora_ms();
    CHECK(ph_col_riconnetti_ora(c, NULL) == 0);
    CHECK(attendi_stato(c, PH_COL_COLLEGATO, 3000));
    CHECK(ora_ms() - t0 < 2000);
    CHECK(finto_collega_chiamate() > coll);
    CHECK(ph_col_chiudi(c) == 0);
    nessun_oggetto_vivo();
}

static void prova_blocco_senza_caduta(void) {
    puts("blocco e sblocco senza caduta");
    un_telefono();
    PhColOpzioni o = veloci();
    PhCollegamento *c = avvia(&o);
    CHECK(attendi_stato(c, PH_COL_COLLEGATO, 3000));
    const void *adb = finto_ultimo_adb();
    finto_tel_bloccato(0, true);
    CHECK(attendi_stato(c, PH_COL_BLOCCATO, 3000));
    PhColAvviso a;
    ph_col_stato(c, &a);
    CHECK(a.ha_telefono);
    PhErrore e = {{0}};
    PhColPrestito *p = ph_col_presta(c, &e);
    CHECK(p != NULL); /* bloccato ma collegato: gli oggetti ci sono */
    if (p) ph_col_restituisci(p);
    finto_tel_bloccato(0, false);
    CHECK(attendi_stato(c, PH_COL_COLLEGATO, 3000));
    CHECK(finto_ultimo_adb() == adb && finto_vivi_adb() == 1); /* stessa connessione */
    char seq[32];
    ev_seq(seq, sizeof seq);
    CHECK(strcmp(seq, "CKBK") == 0);
    CHECK(ph_col_chiudi(c) == 0);
    nessun_oggetto_vivo();

    puts("collegamento a telefono gia' bloccato");
    un_telefono();
    finto_tel_bloccato(0, true);
    c = avvia(&o);
    CHECK(attendi_stato(c, PH_COL_BLOCCATO, 3000));
    ph_col_stato(c, &a);
    CHECK(a.ha_telefono);
    CHECK(ph_col_chiudi(c) == 0);
    nessun_oggetto_vivo();
}

static void prova_samsung(void) {
    puts("bloccato -> caduta -> sblocco -> riconnessione");
    un_telefono();
    PhColOpzioni o = veloci();
    PhCollegamento *c = avvia(&o);
    CHECK(attendi_stato(c, PH_COL_COLLEGATO, 3000));
    finto_tel_bloccato(0, true);
    CHECK(attendi_stato(c, PH_COL_BLOCCATO, 3000));
    finto_tel_acceso(0, false); /* il blocco fa cadere il debug wireless */
    CHECK(attendi_stato(c, PH_COL_PERSO, 3000));
    PhColAvviso a;
    ph_col_stato(c, &a);
    CHECK(strstr(a.motivo, "bloccato") != NULL);
    int n = finto_collega_chiamate();
    CHECK(attendi_collega(n + 3, 3000));
    ph_col_stato(c, &a);
    CHECK(a.stato == PH_COL_PERSO); /* finche' e' bloccato non torna */
    finto_tel_bloccato(0, false);   /* sblocco: il debug wireless riparte */
    finto_tel_acceso(0, true);
    CHECK(attendi_stato(c, PH_COL_COLLEGATO, 3000));
    char seq[32];
    ev_seq(seq, sizeof seq);
    CHECK(strcmp(seq, "CKBPK") == 0);

    /* variante: la riconnessione trova il telefono ancora bloccato */
    finto_tel_bloccato(0, true);
    CHECK(attendi_stato(c, PH_COL_BLOCCATO, 3000));
    finto_tel_acceso(0, false);
    CHECK(attendi_stato(c, PH_COL_PERSO, 3000));
    finto_tel_acceso(0, true);
    CHECK(attendi_stato(c, PH_COL_BLOCCATO, 3000));
    finto_tel_bloccato(0, false);
    CHECK(attendi_stato(c, PH_COL_COLLEGATO, 3000));
    CHECK(ph_col_chiudi(c) == 0);
    nessun_oggetto_vivo();
}

static void prova_componente_muore(void) {
    puts("componente che muore");
    un_telefono();
    PhColOpzioni o = veloci();
    PhCollegamento *c = avvia(&o);
    CHECK(attendi_stato(c, PH_COL_COLLEGATO, 3000));
    /* Subito dopo PERSO il collegamento ritenta: la connessione c'e' (solo il componente
     * e' morto), quindi una ph_adb_collega nuova puo' essere viva per un istante. Qui
     * l'intreccio e' forzato (avvio lento): il controllo non guarda il conteggio delle
     * connessioni in quel momento, ma che la sessione MORTA sia stata rilasciata in
     * ordine (dati, componente, connessione), come dice il registro del finto. */
    finto_avvia_ritardo_ms(100);
    finto_tel_componente_vivo(0, false);
    CHECK(attendi_stato(c, PH_COL_PERSO, 3000));
    PhColAvviso a;
    ph_col_stato(c, &a);
    CHECK(strstr(a.motivo, "componente") != NULL);
    char ord[32];
    finto_ordine_rilascio(ord, sizeof ord);
    CHECK(strncmp(ord, "DCA", 3) == 0); /* la sessione morta e' stata rilasciata per prima */
    CHECK(finto_vivi_componenti() == 0 && finto_vivi_dati() == 0);
    /* La connessione della sessione morta e' chiusa (la "A" del registro); quelle dei
     * tentativi seguenti nascono e muoiono di continuo (avvio lento forzato): il loro
     * conteggio non si guarda qui, e a fine prova nessun oggetto deve restare vivo. */
    finto_tel_componente_vivo(0, true);
    CHECK(attendi_stato(c, PH_COL_COLLEGATO, 3000));
    CHECK(finto_vivi_adb() == 1 && finto_vivi_componenti() == 1);
    CHECK(ph_col_chiudi(c) == 0);
    nessun_oggetto_vivo();

    puts("componente che non parte");
    un_telefono();
    finto_tel_componente_fallisce(0, true);
    c = avvia(&o);
    long fine = ora_ms() + 3000;
    while (finto_avvia_chiamate() < 3 && ora_ms() < fine) dormi(2);
    CHECK(finto_avvia_chiamate() >= 3);
    ph_col_stato(c, &a);
    CHECK(a.stato == PH_COL_CERCO && strstr(a.motivo, "componente"));
    CHECK(finto_vivi_adb() <= 1); /* mai connessioni lasciate aperte */
    finto_tel_componente_fallisce(0, false);
    CHECK(attendi_stato(c, PH_COL_COLLEGATO, 3000));
    CHECK(ph_col_chiudi(c) == 0);
    nessun_oggetto_vivo();
}

static void prova_chiusura_in_ogni_stato(void) {
    puts("chiusura in ogni stato (con intervalli lunghi: le attese si interrompono)");
    PhColOpzioni o = veloci();
    o.pausa_cerca_ms = 5000;
    o.riprova_iniziale_ms = 5000;
    o.riprova_massimo_ms = 5000;
    o.controllo_ms = 5000;
    char seq[32];

    /* CERCO */
    un_telefono();
    finto_tel_acceso(0, false);
    PhCollegamento *c = avvia(&o);
    long fine = ora_ms() + 3000;
    while (finto_scoperta_chiamate() < 1 && ora_ms() < fine) dormi(2);
    for (;;) { /* aspetta l'avviso CERCO con il motivo: il thread e' nella sua pausa */
        PhColAvviso av;
        ph_col_stato(c, &av);
        if ((av.stato == PH_COL_CERCO && strstr(av.motivo, "nessun telefono")) || ora_ms() > fine) break;
        dormi(2);
    }
    long t0 = ora_ms();
    CHECK(ph_col_chiudi(c) == 0);
    CHECK(ora_ms() - t0 < 1000);
    CHECK(ev_ultimo().stato == PH_COL_CHIUSO);
    nessun_oggetto_vivo();

    /* COLLEGATO */
    un_telefono();
    c = avvia(&o);
    CHECK(attendi_stato(c, PH_COL_COLLEGATO, 3000));
    t0 = ora_ms();
    CHECK(ph_col_chiudi(c) == 0);
    CHECK(ora_ms() - t0 < 1000);
    CHECK(ev_ultimo().stato == PH_COL_CHIUSO);
    nessun_oggetto_vivo();

    /* BLOCCATO (qui il controllo e' veloce, per vedere il blocco) */
    un_telefono();
    finto_tel_bloccato(0, true);
    c = avvia(&o);
    CHECK(attendi_stato(c, PH_COL_BLOCCATO, 3000));
    CHECK(ph_col_chiudi(c) == 0);
    CHECK(ev_ultimo().stato == PH_COL_CHIUSO);
    nessun_oggetto_vivo();

    /* PERSO */
    PhColOpzioni v = veloci();
    v.riprova_iniziale_ms = 5000;
    v.riprova_massimo_ms = 5000;
    un_telefono();
    c = avvia(&v);
    CHECK(attendi_stato(c, PH_COL_COLLEGATO, 3000));
    finto_tel_acceso(0, false);
    CHECK(attendi_stato(c, PH_COL_PERSO, 3000));
    { /* il tentativo immediato e' fallito: il thread e' nell'attesa da 5 s */
        long f2 = ora_ms() + 3000;
        while (finto_collega_fallite() < 1 && ora_ms() < f2) dormi(1);
        CHECK(finto_collega_fallite() >= 1);
    }
    t0 = ora_ms();
    CHECK(ph_col_chiudi(c) == 0);
    CHECK(ora_ms() - t0 < 1000);
    ev_seq(seq, sizeof seq);
    CHECK(strcmp(seq, "CKPX") == 0);
    nessun_oggetto_vivo();

    /* NON_ABBINATO */
    un_telefono();
    finto_tel_abbinato(0, false);
    c = avvia(&o);
    CHECK(attendi_stato(c, PH_COL_NON_ABBINATO, 3000));
    t0 = ora_ms();
    CHECK(ph_col_chiudi(c) == 0);
    CHECK(ora_ms() - t0 < 1000);
    CHECK(ev_ultimo().stato == PH_COL_CHIUSO);
    nessun_oggetto_vivo();

    /* subito dopo l'avvio, senza aspettare niente */
    un_telefono();
    c = avvia(&o);
    CHECK(ph_col_chiudi(c) == 0);
    nessun_oggetto_vivo();
}

static void prova_chiusura_a_meta_tentativo(void) {
    puts("chiusura a meta' tentativo");
    PhColOpzioni o = veloci();
    /* dentro ph_adb_collega */
    un_telefono();
    finto_collega_ritardo_ms(300);
    PhCollegamento *c = avvia(&o);
    CHECK(attendi_collega(1, 3000));
    long t0 = ora_ms();
    CHECK(ph_col_chiudi(c) == 0);
    CHECK(ora_ms() - t0 < 2000);
    CHECK(ev_ultimo().stato == PH_COL_CHIUSO);
    CHECK(finto_avvia_chiamate() == 0);
    nessun_oggetto_vivo();

    /* dentro ph_componente_avvia: il componente nato a chiusura chiesta si rilascia in ordine */
    un_telefono();
    finto_avvia_ritardo_ms(300);
    c = avvia(&o);
    long fine = ora_ms() + 3000;
    while (finto_avvia_chiamate() < 1 && ora_ms() < fine) dormi(2);
    CHECK(finto_avvia_chiamate() == 1);
    CHECK(ph_col_chiudi(c) == 0);
    CHECK(ev_ultimo().stato == PH_COL_CHIUSO);
    CHECK(ev_conta_stato(PH_COL_COLLEGATO) == 0); /* nessun avviso di collegato a porte chiuse */
    char ord[32];
    finto_ordine_rilascio(ord, sizeof ord);
    CHECK(strcmp(ord, "DCA") == 0);
    nessun_oggetto_vivo();

    /* dentro la scoperta */
    un_telefono();
    finto_tel_acceso(0, false);
    finto_scoperta_ritardo_ms(300);
    o.durata_scoperta_ms = 300;
    c = avvia(&o);
    fine = ora_ms() + 3000;
    while (finto_scoperta_chiamate() < 1 && ora_ms() < fine) dormi(2);
    t0 = ora_ms();
    CHECK(ph_col_chiudi(c) == 0);
    CHECK(ora_ms() - t0 < 2000);
    nessun_oggetto_vivo();
}

static _Atomic(PhCollegamento *) g_col;
static atomic_int chiudi_da_avviso, chiudi_da_perdita;
static void avviso_che_chiude(const PhColAvviso *a) {
    if (a->stato == PH_COL_COLLEGATO) {
        while (!atomic_load(&g_col)) dormi(1); /* avvia non e' ancora tornata */
        atomic_store(&chiudi_da_avviso, ph_col_chiudi(atomic_load(&g_col)) == -1 ? 1 : 2);
    }
}
static void perdita_che_chiude(void) {
    atomic_store(&chiudi_da_perdita, ph_col_chiudi(atomic_load(&g_col)) == -1 ? 1 : 2);
}

static void prova_chiusura_dall_avviso(void) {
    puts("chiusura chiamata da dentro un avviso");
    un_telefono();
    PhColOpzioni o = veloci();
    atomic_store(&chiudi_da_avviso, 0);
    atomic_store(&chiudi_da_perdita, 0);
    hook_avviso = avviso_che_chiude;
    hook_perdita = perdita_che_chiude;
    atomic_store(&g_col, NULL);
    PhCollegamento *c = avvia(&o);
    atomic_store(&g_col, c);
    CHECK(attendi_stato(c, PH_COL_COLLEGATO, 3000));
    long fine = ora_ms() + 3000;
    while (atomic_load(&chiudi_da_avviso) == 0 && ora_ms() < fine) dormi(2);
    CHECK(atomic_load(&chiudi_da_avviso) == 1); /* rifiutata, nessuno stallo */
    CHECK(ph_col_chiudi(c) == 0);               /* da fuori funziona; chiama in_perdita */
    CHECK(atomic_load(&chiudi_da_perdita) == 1);
    hook_avviso = NULL;
    hook_perdita = NULL;
    nessun_oggetto_vivo();
}

/* ---- Prestiti ------------------------------------------------------------------------*/
static _Atomic(PhColPrestito *) g_prestito;
static atomic_int perdita_attivo, perdita_presta_null;
static void perdita_che_restituisce(void) {
    PhColPrestito *p = atomic_exchange(&g_prestito, NULL);
    if (!p) return;
    atomic_store(&perdita_attivo, ph_col_prestito_attivo(p) ? 1 : 0);
    /* a questo punto non si ottengono piu' prestiti */
    /* g_col e' valido: il collegamento vive */
    PhColPrestito *altro = ph_col_presta(atomic_load(&g_col), NULL);
    atomic_store(&perdita_presta_null, altro == NULL);
    if (altro) ph_col_restituisci(altro);
    ph_col_restituisci(p);
}

static void prova_prestiti_rilasciati(void) {
    puts("prestiti: restituiti dall'avviso 'sto per perdere'");
    un_telefono();
    PhColOpzioni o = veloci();
    o.attesa_prestiti_ms = 5000;
    hook_perdita = perdita_che_restituisce;
    atomic_store(&g_col, NULL);
    atomic_store(&perdita_attivo, -1);
    atomic_store(&perdita_presta_null, 0);
    PhCollegamento *c = avvia(&o);
    atomic_store(&g_col, c);
    CHECK(attendi_stato(c, PH_COL_COLLEGATO, 3000));
    PhColPrestito *p = ph_col_presta(c, NULL);
    CHECK(p != NULL);
    CHECK(ph_col_prestito_adb(p) != NULL);
    atomic_store(&g_prestito, p);
    long t0 = ora_ms();
    finto_tel_acceso(0, false);
    CHECK(attendi_stato(c, PH_COL_PERSO, 3000));
    CHECK(ora_ms() - t0 < 2500); /* non ha aspettato il tempo massimo */
    CHECK(atomic_load(&perdita_attivo) == 0);
    CHECK(atomic_load(&perdita_presta_null) == 1);
    CHECK(finto_vivi_adb() == 0 && finto_vivi_componenti() == 0 && finto_vivi_dati() == 0);
    CHECK(ph_col_presta(c, NULL) == NULL);
    CHECK(ph_col_chiudi(c) == 0);
    hook_perdita = NULL;
    nessun_oggetto_vivo();
}

typedef struct {
    PhColPrestito *p;
    int ms;
} Tardivo;
static void *restituisce_tardi(void *a) {
    Tardivo *t = a;
    dormi(t->ms);
    ph_col_restituisci(t->p);
    return NULL;
}

static void prova_prestiti_tardivi(void) {
    puts("prestiti: restituito da un altro thread durante l'attesa");
    un_telefono();
    PhColOpzioni o = veloci();
    o.attesa_prestiti_ms = 5000;
    PhCollegamento *c = avvia(&o);
    CHECK(attendi_stato(c, PH_COL_COLLEGATO, 3000));
    PhColPrestito *p1 = ph_col_presta(c, NULL);
    PhColPrestito *p2 = ph_col_presta(c, NULL);
    CHECK(p1 && p2);
    Tardivo t1 = {p1, 30}, t2 = {p2, 60};
    pthread_t th1, th2;
    pthread_create(&th1, NULL, restituisce_tardi, &t1);
    pthread_create(&th2, NULL, restituisce_tardi, &t2);
    long t0 = ora_ms();
    finto_tel_acceso(0, false);
    CHECK(attendi_stato(c, PH_COL_PERSO, 3000));
    CHECK(ora_ms() - t0 < 2500);
    CHECK(finto_vivi_adb() == 0); /* distrutto dal collegamento, non orfano */
    pthread_join(th1, NULL);
    pthread_join(th2, NULL);
    CHECK(ph_col_chiudi(c) == 0);
    nessun_oggetto_vivo();
}

static void prova_prestiti_non_restituiti(void) {
    puts("prestiti: non restituiti alla caduta (sessione orfana)");
    un_telefono();
    PhColOpzioni o = veloci();
    o.attesa_prestiti_ms = 50;
    PhCollegamento *c = avvia(&o);
    CHECK(attendi_stato(c, PH_COL_COLLEGATO, 3000));
    PhColPrestito *p = ph_col_presta(c, NULL);
    CHECK(p != NULL);
    PhAdb *adb = ph_col_prestito_adb(p);
    PhComponente *comp = ph_col_prestito_componente(p);
    finto_tel_acceso(0, false);
    CHECK(attendi_stato(c, PH_COL_PERSO, 3000));
    /* gli oggetti sono ancora memoria valida ma morti */
    CHECK(finto_vivi_adb() == 1 && finto_vivi_componenti() == 1 && finto_vivi_dati() == 1);
    CHECK(!ph_col_prestito_attivo(p));
    CHECK(!ph_adb_viva(adb));
    CHECK(!ph_componente_vivo(comp));
    CHECK(ph_col_presta(c, NULL) == NULL);
    finto_tel_acceso(0, true);
    CHECK(attendi_stato(c, PH_COL_COLLEGATO, 3000)); /* il collegamento e' ripartito comunque */
    CHECK(finto_vivi_adb() == 2);
    CHECK(!ph_col_prestito_attivo(p));
    PhColPrestito *nuovo = ph_col_presta(c, NULL);
    CHECK(nuovo != NULL && ph_col_prestito_adb(nuovo) != adb);
    if (nuovo) ph_col_restituisci(nuovo);
    ph_col_restituisci(p); /* l'ultimo che restituisce distrugge l'orfano */
    CHECK(finto_vivi_adb() == 1 && finto_vivi_componenti() == 1 && finto_vivi_dati() == 1);
    char ord[32];
    finto_ordine_rilascio(ord, sizeof ord);
    CHECK(strcmp(ord, "DCA") == 0);
    CHECK(ph_col_chiudi(c) == 0);
    nessun_oggetto_vivo();

    puts("prestiti: non restituiti alla chiusura");
    un_telefono();
    c = avvia(&o);
    CHECK(attendi_stato(c, PH_COL_COLLEGATO, 3000));
    PhColPrestito *a = ph_col_presta(c, NULL);
    PhColPrestito *b = ph_col_presta(c, NULL);
    CHECK(a && b);
    CHECK(ph_col_chiudi(c) == 1); /* collegamento libero, sessione orfana */
    CHECK(finto_vivi_adb() == 1);
    CHECK(!ph_col_prestito_attivo(a));
    ph_col_restituisci(a);
    CHECK(finto_vivi_adb() == 1); /* c'e' ancora b */
    ph_col_restituisci(b);
    nessun_oggetto_vivo();
    finto_ordine_rilascio(ord, sizeof ord);
    CHECK(strcmp(ord, "DCA") == 0);
}

/* ---- Piu' telefoni --------------------------------------------------------------------*/
static void mondo_tre(void) {
    finto_reset();
    finto_tel(0, "10.0.0.1", 5555, "SER0000AAA", "Mod-A");
    finto_tel(1, "10.0.0.2", 5555, "SER0000BBB", "Mod-B");
    finto_tel(2, "10.0.0.3", 5555, "SER0000CCC", "Mod-C");
    int l[] = {0, 1, 2};
    finto_scoperta_telefoni(l, 3);
}

static void prova_piu_telefoni(void) {
    puts("piu' telefoni in rete");
    PhColOpzioni o = veloci();
    PhColAvviso a;

    /* nessuno conosciuto: il primo, e lo dice */
    mondo_tre();
    PhCollegamento *c = avvia(&o);
    CHECK(attendi_stato(c, PH_COL_COLLEGATO, 3000));
    ph_col_stato(c, &a);
    CHECK(strcmp(a.telefono.seriale, "SER0000AAA") == 0);
    CHECK(a.telefoni_trovati == 3 && a.scelta_arbitraria);
    CHECK(strstr(a.motivo, "scelto il primo") != NULL);
    CHECK(ph_col_chiudi(c) == 0);
    nessun_oggetto_vivo();

    /* due conosciuti (indirizzi vecchi): vince il visto per ultimo */
    mondo_tre();
    finto_noti_aggiungi("SER0000BBB", "Mod-B", "10.9.9.9:1", 100);
    finto_noti_aggiungi("SER0000CCC", "Mod-C", "10.9.9.9:2", 200);
    c = avvia(&o);
    CHECK(attendi_stato(c, PH_COL_COLLEGATO, 3000));
    ph_col_stato(c, &a);
    CHECK(strcmp(a.telefono.seriale, "SER0000CCC") == 0);
    CHECK(a.telefoni_trovati == 3 && !a.scelta_arbitraria);
    CHECK(a.telefono.conosciuto);
    CHECK(ph_col_chiudi(c) == 0);
    nessun_oggetto_vivo();

    /* nomi mDNS che non dicono il seriale e nessun conosciuto riconoscibile: il primo */
    mondo_tre();
    finto_scoperta_nomi(false);
    finto_noti_aggiungi("SER0000CCC", "Mod-C", "10.9.9.9:2", 200);
    c = avvia(&o);
    CHECK(attendi_stato(c, PH_COL_COLLEGATO, 3000));
    ph_col_stato(c, &a);
    CHECK(strcmp(a.telefono.seriale, "SER0000AAA") == 0 && a.scelta_arbitraria);
    CHECK(ph_col_chiudi(c) == 0);
    nessun_oggetto_vivo();

    /* ricollegamento: un altro telefono allo stesso indirizzo non va bene */
    mondo_tre();
    c = avvia(&o);
    CHECK(attendi_stato(c, PH_COL_COLLEGATO, 3000));
    finto_tel_acceso(0, false);
    CHECK(attendi_stato(c, PH_COL_PERSO, 3000));
    finto_tel_seriale(0, "SER0000ZZZ"); /* ora l'indirizzo e' di un altro */
    finto_tel_acceso(0, true);
    int n = finto_collega_chiamate();
    CHECK(attendi_collega(n + 3, 3000));
    ph_col_stato(c, &a);
    CHECK(a.stato == PH_COL_PERSO); /* ne' lui ne' B e C: sono telefoni diversi */
    long fine = ora_ms() + 1000; /* le connessioni sbagliate si chiudono subito */
    while (finto_vivi_adb() != 0 && ora_ms() < fine) dormi(1);
    CHECK(finto_vivi_adb() == 0);
    finto_tel_seriale(0, "SER0000AAA");
    CHECK(attendi_stato(c, PH_COL_COLLEGATO, 3000));
    ph_col_stato(c, &a);
    CHECK(strcmp(a.telefono.seriale, "SER0000AAA") == 0);
    CHECK(ph_col_chiudi(c) == 0);
    nessun_oggetto_vivo();
}

static void prova_diretto_e_config(void) {
    puts("ultimo indirizzo noto, IPv6, aggiornamento di telefoni.toml");
    PhColOpzioni o = veloci();
    PhColAvviso a;

    un_telefono();
    finto_scoperta_telefoni(NULL, 0); /* la rete non dice nulla */
    finto_noti_aggiungi("R5CT0000000", "Vecchio", "10.0.0.1:5555", 10);
    PhCollegamento *c = avvia(&o);
    CHECK(attendi_stato(c, PH_COL_COLLEGATO, 3000));
    ph_col_stato(c, &a);
    CHECK(a.telefono.conosciuto);
    CHECK(finto_scoperta_chiamate() == 0); /* si e' collegato dall'ultimo indirizzo */
    FintoNoto n;
    CHECK(finto_noti_cerca("R5CT0000000", &n));
    CHECK(strcmp(n.nome, "Modello-X") == 0 && n.visto > 10);
    CHECK(ph_col_chiudi(c) == 0);
    nessun_oggetto_vivo();

    finto_reset();
    finto_tel(0, "fe80::1", 5555, "R5CT0000000", "Modello-X");
    c = avvia(&o); /* indirizzo IPv6 trovato dalla scoperta */
    int l[] = {0};
    finto_scoperta_telefoni(l, 1);
    CHECK(attendi_stato(c, PH_COL_COLLEGATO, 3000));
    CHECK(finto_noti_cerca("R5CT0000000", &n));
    CHECK(strcmp(n.indirizzo, "[fe80::1]:5555") == 0);
    finto_tel_acceso(0, false);
    CHECK(attendi_stato(c, PH_COL_PERSO, 3000));
    finto_scoperta_telefoni(NULL, 0);
    finto_tel_acceso(0, true); /* si riprende dall'indirizzo salvato, con le parentesi */
    CHECK(attendi_stato(c, PH_COL_COLLEGATO, 3000));
    CHECK(ph_col_chiudi(c) == 0);
    nessun_oggetto_vivo();

    puts("salvataggio che fallisce: non e' fatale");
    un_telefono();
    finto_noti_rifiuta_salvataggio(true);
    c = avvia(&o);
    CHECK(attendi_stato(c, PH_COL_COLLEGATO, 3000));
    CHECK(finto_noti_salvataggi() == 0);
    CHECK(ph_col_chiudi(c) == 0);
    nessun_oggetto_vivo();
}

static void prova_numero_di_serie(void) {
    puts("numero di serie: ripieghi");
    PhColOpzioni o = veloci();
    PhColAvviso a;

    un_telefono();
    finto_tel_seriale(0, "unknown");
    finto_tel_android_id(0, "ab cd");
    PhCollegamento *c = avvia(&o);
    CHECK(attendi_stato(c, PH_COL_COLLEGATO, 3000));
    ph_col_stato(c, &a);
    CHECK(strcmp(a.telefono.seriale, "aid-ab_cd") == 0);
    CHECK(ph_col_chiudi(c) == 0);

    un_telefono();
    finto_tel_seriale(0, "");
    c = avvia(&o);
    CHECK(attendi_stato(c, PH_COL_COLLEGATO, 3000));
    ph_col_stato(c, &a);
    CHECK(strcmp(a.telefono.seriale, "modello-Modello-X") == 0);
    CHECK(ph_col_chiudi(c) == 0);
    nessun_oggetto_vivo();

    puts("fuori stato: riconnetti e abbina rifiutati; opzioni");
    un_telefono();
    c = avvia(&o);
    CHECK(attendi_stato(c, PH_COL_COLLEGATO, 3000));
    CHECK(ph_col_abbina(c, "123456", NULL) == -1);
    CHECK(ph_col_riconnetti_ora(c, NULL) == -1);
    CHECK(ph_col_chiudi(c) == 0);
    PhErrore e = {{0}};
    PhColOpzioni senza = veloci();
    senza.jar_locale = NULL;
    CHECK(ph_col_avvia(&senza, &e) == NULL && e.testo[0]);
    nessun_oggetto_vivo();
}

static int conta_esiti(PhColEsitoAbbinamento e) {
    int k = 0;
    pthread_mutex_lock(&Mev);
    for (int i = 0; i < ev_n; i++)
        if (ev[i].esito_abbinamento == e) k++;
    pthread_mutex_unlock(&Mev);
    return k;
}
static atomic_int snap_ok;
static _Atomic(PhCollegamento *) g_col2;
static void hook_snapshot(const PhColAvviso *a) {
    if (a->esito_abbinamento != PH_COL_ABB_NESSUNO) {
        PhColAvviso f;
        ph_col_stato(atomic_load(&g_col2), &f);
        atomic_store(&snap_ok, f.esito_abbinamento == PH_COL_ABB_NESSUNO ? 1 : 2);
    }
}

typedef struct {
    PhCollegamento *c;
    atomic_bool stop;
    atomic_int accettate;
} Martello;
static void *martella(void *a) {
    Martello *m = a;
    while (!atomic_load(&m->stop))
        if (ph_col_abbina(m->c, "123456", NULL) == 0) atomic_fetch_add(&m->accettate, 1);
    return NULL;
}
static long cpu_ms(void) {
    struct timespec t;
    clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &t);
    return (long)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

static void prova_abbina_concorrente(void) {
    puts("richieste di abbinamento a raffica: ognuna accettata ha il suo esito, il thread non gira a vuoto");
    PhColOpzioni o = veloci();
    finto_reset();
    finto_tel(0, "10.0.0.1", 5555, "R5CT0000000", "Modello-X");
    finto_tel_abbinato(0, false);
    finto_abbinamento_telefono(0);
    int l[] = {0};
    finto_scoperta_telefoni(l, 1);
    finto_abbinamento_ritardo_ms(20);
    hook_avviso = hook_snapshot;
    atomic_store(&g_col2, NULL);
    atomic_store(&snap_ok, 0);
    PhCollegamento *c = avvia(&o);
    atomic_store(&g_col2, c);
    CHECK(attendi_stato(c, PH_COL_NON_ABBINATO, 3000));
    Martello m = {c, false, 0};
    pthread_t th;
    pthread_create(&th, NULL, martella, &m);
    CHECK(attendi_stato(c, PH_COL_COLLEGATO, 5000));
    atomic_store(&m.stop, true);
    pthread_join(th, NULL);
    int acc = atomic_load(&m.accettate);
    CHECK(acc >= 1);
    /* ogni richiesta accettata ha avuto UN esito: nessuna appesa */
    CHECK(conta_esiti(PH_COL_ABB_RIUSCITO) + conta_esiti(PH_COL_ABB_FALLITO) == acc);
    CHECK(atomic_load(&snap_ok) == 1); /* ph_col_stato non ripete l'esito */
    /* niente giri a vuoto: in 400 ms di quiete il processo usa poco processore */
    long c0 = cpu_ms();
    long fine = ora_ms() + 400;
    while (ora_ms() < fine) dormi(10);
    CHECK(cpu_ms() - c0 < 150);
    CHECK(ph_col_chiudi(c) == 0);
    hook_avviso = NULL;
    nessun_oggetto_vivo();
}

static void prova_esito_abbinamento(void) {
    puts("esito dell'abbinamento: uno e un solo avviso per richiesta");
    PhColOpzioni o = veloci();
    finto_reset();
    finto_tel(0, "10.0.0.1", 5555, "R5CT0000000", "Modello-X");
    finto_tel_abbinato(0, false);
    finto_abbinamento_telefono(0);
    int l[] = {0};
    finto_scoperta_telefoni(l, 1);
    PhCollegamento *c = avvia(&o);
    CHECK(attendi_stato(c, PH_COL_NON_ABBINATO, 3000));
    CHECK(conta_esiti(PH_COL_ABB_FALLITO) + conta_esiti(PH_COL_ABB_RIUSCITO) == 0);

    /* a) codice sbagliato, con l'abbinamento "in corso": riconnetti ora e' rifiutato */
    finto_abbinamento_ritardo_ms(100);
    CHECK(ph_col_abbina(c, "000000", NULL) == 0);
    long fine = ora_ms() + 3000;
    while (finto_abbinamento_chiamate() < 1 && ora_ms() < fine) dormi(1);
    CHECK(finto_abbinamento_chiamate() == 1);
    CHECK(ph_col_riconnetti_ora(c, NULL) == -1);
    CHECK(ph_col_abbina(c, "123456", NULL) == -1); /* gia' in corso */
    fine = ora_ms() + 3000;
    while (conta_esiti(PH_COL_ABB_FALLITO) < 1 && ora_ms() < fine) dormi(1);
    CHECK(conta_esiti(PH_COL_ABB_FALLITO) == 1 && conta_esiti(PH_COL_ABB_RIUSCITO) == 0);
    finto_abbinamento_ritardo_ms(0);

    /* b) servizio non visibile: fallito, un solo avviso */
    finto_abbinamento_visibile(false);
    CHECK(ph_col_abbina(c, "123456", NULL) == 0);
    fine = ora_ms() + 3000;
    while (conta_esiti(PH_COL_ABB_FALLITO) < 2 && ora_ms() < fine) dormi(1);
    CHECK(conta_esiti(PH_COL_ABB_FALLITO) == 2);
    finto_abbinamento_visibile(true);

    /* c) "Cerca di nuovo" e subito un abbinamento: l'abbinamento e' rifiutato, il CERCO
     * della ricerca non e' mai l'esito di un abbinamento */
    int coll = finto_collega_chiamate();
    CHECK(ph_col_riconnetti_ora(c, NULL) == 0);
    CHECK(ph_col_abbina(c, "123456", NULL) == -1);
    CHECK(attendi_collega(coll + 1, 3000));
    fine = ora_ms() + 3000;
    while (ev_conta_stato(PH_COL_NON_ABBINATO) < 4 && ora_ms() < fine) dormi(1);
    CHECK(ev_conta_stato(PH_COL_NON_ABBINATO) >= 4);
    CHECK(attendi_stato(c, PH_COL_NON_ABBINATO, 3000));
    CHECK(conta_esiti(PH_COL_ABB_FALLITO) == 2 && conta_esiti(PH_COL_ABB_RIUSCITO) == 0);

    /* d) codice giusto: l'avviso che porta a CERCO e' l'unico RIUSCITO */
    CHECK(ph_col_abbina(c, "123456", NULL) == 0);
    CHECK(attendi_stato(c, PH_COL_COLLEGATO, 3000));
    CHECK(conta_esiti(PH_COL_ABB_RIUSCITO) == 1 && conta_esiti(PH_COL_ABB_FALLITO) == 2);
    pthread_mutex_lock(&Mev);
    for (int i = 0; i < ev_n; i++)
        if (ev[i].esito_abbinamento == PH_COL_ABB_RIUSCITO) {
            CHECK(ev[i].stato == PH_COL_CERCO);
            CHECK(i + 1 < ev_n && ev[i + 1].esito_abbinamento == PH_COL_ABB_NESSUNO);
        }
    pthread_mutex_unlock(&Mev);
    CHECK(ph_col_chiudi(c) == 0);
    nessun_oggetto_vivo();

    puts("chiusura durante un abbinamento: nessun avviso lo conclude");
    finto_reset();
    finto_tel(0, "10.0.0.1", 5555, "R5CT0000000", "Modello-X");
    finto_tel_abbinato(0, false);
    finto_abbinamento_telefono(0);
    finto_scoperta_telefoni(l, 1);
    c = avvia(&o);
    CHECK(attendi_stato(c, PH_COL_NON_ABBINATO, 3000));
    finto_abbinamento_ritardo_ms(200);
    CHECK(ph_col_abbina(c, "123456", NULL) == 0);
    fine = ora_ms() + 3000;
    while (finto_abbinamento_chiamate() < 1 && ora_ms() < fine) dormi(1);
    CHECK(finto_abbinamento_chiamate() == 1);
    CHECK(ph_col_chiudi(c) == 0);
    CHECK(conta_esiti(PH_COL_ABB_RIUSCITO) + conta_esiti(PH_COL_ABB_FALLITO) == 0);
    CHECK(ev_ultimo().stato == PH_COL_CHIUSO);
    nessun_oggetto_vivo();
}

static void prova_giro_abbinamento(void) {
    puts("giro intero: abbinamento, nuova istanza, chiave revocata");
    PhColOpzioni o = veloci();
    PhColAvviso a;
    char seq[32];

    /* nessuna chiave, nessun telefono noto, PC non abbinato */
    finto_reset();
    finto_tel(0, "10.0.0.1", 5555, "R5CT0000000", "Modello-X");
    finto_tel_abbinato(0, false);
    finto_abbinamento_telefono(0);
    int l[] = {0};
    finto_scoperta_telefoni(l, 1);
    CHECK(finto_chiavi_create() == 0 && finto_noti_conta() == 0);
    PhCollegamento *c = avvia(&o);
    CHECK(attendi_stato(c, PH_COL_NON_ABBINATO, 3000));
    CHECK(finto_chiavi_create() == 1); /* la chiave si crea al primo bisogno */
    CHECK(ph_col_abbina(c, "123456", NULL) == 0);
    CHECK(attendi_stato(c, PH_COL_COLLEGATO, 3000));
    CHECK(finto_noti_conta() == 1);
    FintoNoto n1;
    CHECK(finto_noti_cerca("R5CT0000000", &n1));
    CHECK(ph_col_chiudi(c) == 0);
    nessun_oggetto_vivo();

    /* NUOVA istanza sugli stessi file: chiave e telefono gia' li', mai NON_ABBINATO */
    c = avvia(&o);
    CHECK(attendi_stato(c, PH_COL_COLLEGATO, 3000));
    ev_seq(seq, sizeof seq);
    CHECK(strcmp(seq, "CK") == 0);
    CHECK(finto_chiavi_create() == 1); /* la chiave e' stata riletta, non rifatta */
    CHECK(finto_noti_conta() == 1);
    ph_col_stato(c, &a);
    CHECK(a.telefono.conosciuto);
    CHECK(ph_col_chiudi(c) == 0);
    nessun_oggetto_vivo();

    /* telefono noto, ma ha dimenticato la chiave: NON_ABBINATO (c'e' un'uscita) */
    finto_noti_imposta_visto("R5CT0000000", 100);
    finto_tel_revoca(0);
    c = avvia(&o);
    CHECK(attendi_stato(c, PH_COL_NON_ABBINATO, 3000));
    CHECK(finto_vivi_adb() == 0);
    CHECK(ph_col_abbina(c, "123456", NULL) == 0);
    CHECK(attendi_stato(c, PH_COL_COLLEGATO, 3000));
    CHECK(finto_noti_conta() == 1); /* nessuna voce doppia */
    CHECK(finto_noti_cerca("R5CT0000000", &n1) && n1.visto > 100); /* aggiornato */
    ev_seq(seq, sizeof seq);
    CHECK(strstr(seq, "NCK") != NULL);
    CHECK(ph_col_chiudi(c) == 0);
    nessun_oggetto_vivo();
}

static int visti_tradotti;
/* Tutti i motivi registrati in questa istanza devono essere passati da ph_t. */
static void controlla_tradotti(void) {
    pthread_mutex_lock(&Mev);
    for (int i = 0; i < ev_n; i++) {
        visti_tradotti++;
        if (strncmp(ev[i].motivo, "[en] ", 5) != 0) {
            printf("motivo non tradotto: %s\n", ev[i].motivo);
            errori++;
        }
    }
    pthread_mutex_unlock(&Mev);
}

static void prova_motivi_tradotti(void) {
    puts("ogni motivo passa dalla traduzione");
    PhColOpzioni o = veloci();
    finto_reset();
    finto_lingua_inglese(true);
    finto_tel(0, "10.0.0.1", 5555, "R5CT0000000", "Modello-X");
    finto_tel(1, "10.0.0.2", 5555, "R5CT0000001", "Modello-Y");
    finto_tel(2, "10.0.0.3", 5555, "R5CT0000002", "Modello-Z");
    finto_tel_acceso(0, false);
    finto_tel_abbinato(0, false);
    finto_abbinamento_telefono(0);
    int l[] = {0, 1, 2};
    finto_scoperta_telefoni(l, 3);
    PhCollegamento *c = avvia(&o);
    CHECK(attendi_stato(c, PH_COL_COLLEGATO, 3000)); /* piu' telefoni: "scelto il primo" */
    finto_tel_bloccato(1, true);
    CHECK(attendi_stato(c, PH_COL_BLOCCATO, 3000));
    finto_tel_bloccato(1, false);
    CHECK(attendi_stato(c, PH_COL_COLLEGATO, 3000));
    finto_tel_acceso(1, false);
    CHECK(attendi_stato(c, PH_COL_PERSO, 3000));
    CHECK(ph_col_chiudi(c) == 0);
    controlla_tradotti();

    /* non abbinato, codice sbagliato, servizio non visibile, riuscito */
    finto_reset();
    finto_lingua_inglese(true);
    finto_tel(0, "10.0.0.1", 5555, "R5CT0000000", "Modello-X");
    finto_tel_abbinato(0, false);
    finto_abbinamento_telefono(0);
    finto_scoperta_telefoni(l, 1);
    c = avvia(&o);
    CHECK(attendi_stato(c, PH_COL_NON_ABBINATO, 3000));
    int n0 = ev_conta_stato(PH_COL_NON_ABBINATO);
    finto_abbinamento_visibile(false);
    CHECK(ph_col_abbina(c, "123456", NULL) == 0);
    long fine = ora_ms() + 3000;
    while (ev_conta_stato(PH_COL_NON_ABBINATO) == n0 && ora_ms() < fine) dormi(1);
    n0 = ev_conta_stato(PH_COL_NON_ABBINATO);
    finto_abbinamento_visibile(true);
    CHECK(ph_col_abbina(c, "000000", NULL) == 0);
    fine = ora_ms() + 3000;
    while (ev_conta_stato(PH_COL_NON_ABBINATO) == n0 && ora_ms() < fine) dormi(1);
    CHECK(ph_col_abbina(c, "123456", NULL) == 0);
    CHECK(attendi_stato(c, PH_COL_COLLEGATO, 3000));
    CHECK(ph_col_chiudi(c) == 0);
    controlla_tradotti();

    /* nessun telefono; componente guasto; chiusura */
    finto_reset();
    finto_lingua_inglese(true);
    finto_tel(0, "10.0.0.1", 5555, "R5CT0000000", "Modello-X");
    finto_tel_acceso(0, false);
    finto_scoperta_telefoni(l, 1);
    c = avvia(&o);
    fine = ora_ms() + 3000;
    while (finto_scoperta_chiamate() < 2 && ora_ms() < fine) dormi(1);
    finto_tel_acceso(0, true);
    finto_tel_componente_fallisce(0, true);
    fine = ora_ms() + 3000;
    while (finto_avvia_chiamate() < 1 && ora_ms() < fine) dormi(1);
    CHECK(ph_col_chiudi(c) == 0);
    controlla_tradotti();

    CHECK(visti_tradotti >= 12);
    finto_lingua_inglese(false);
    nessun_oggetto_vivo();
}

static atomic_int n_stato_tel, n_notifiche, ultimo_bloccato_tel;
static void su_stato_tel(const PhDatiStato *st, void *u) {
    (void)u;
    atomic_store(&ultimo_bloccato_tel, st->bloccato ? 1 : 0);
    atomic_fetch_add(&n_stato_tel, 1);
}
static void su_notifiche(void *u) {
    (void)u;
    atomic_fetch_add(&n_notifiche, 1);
}

static atomic_int n_evento_video;
static void su_evento_video(uint32_t id, uint8_t che, uint16_t a, uint16_t b, void *u) {
    (void)id; (void)che; (void)a; (void)b; (void)u;
    atomic_fetch_add(&n_evento_video, 1);
}
static int tre_conteggi(void) {
    return atomic_load(&n_stato_tel) + atomic_load(&n_notifiche) + atomic_load(&n_evento_video);
}
static void scarica_eventi(void) {
    finto_tel_bloccato(0, true);
    finto_tel_bloccato(0, false);
    finto_tel_notifiche(0);
    finto_tel_evento_video(0);
}

static void prova_silenzio_dopo_residui(void) {
    puts("niente piu' inoltri dopo i residui e dopo la chiusura");
    PhColOpzioni o = veloci();
    o.attesa_prestiti_ms = 50;
    o.stato_telefono = su_stato_tel;
    o.notifiche = su_notifiche;
    o.evento_video = su_evento_video;

    /* residuo: dati rifiuta la chiusura, componente e dati restano agganciati */
    un_telefono();
    atomic_store(&n_stato_tel, 0);
    atomic_store(&n_notifiche, 0);
    atomic_store(&n_evento_video, 0);
    PhCollegamento *c = avvia(&o);
    CHECK(attendi_stato(c, PH_COL_COLLEGATO, 3000));
    scarica_eventi(); /* collegati: passano tutti e tre */
    CHECK(atomic_load(&n_stato_tel) == 2 && atomic_load(&n_notifiche) == 1 &&
          atomic_load(&n_evento_video) == 1);
    finto_dati_rifiuta(true);
    finto_tel_acceso(0, false);
    CHECK(attendi_stato(c, PH_COL_PERSO, 3000));
    CHECK(finto_vivi_dati() == 1 && finto_vivi_componenti() == 1); /* il residuo c'e' */
    int prima = tre_conteggi();
    scarica_eventi();
    CHECK(tre_conteggi() == prima);
    CHECK(ph_col_chiudi(c) == 0);
    scarica_eventi(); /* anche dopo la chiusura */
    CHECK(tre_conteggi() == prima);
    finto_dati_rifiuta(false);
    finto_tel_acceso(0, true);
    c = avvia(&o); /* ritenta e libera il residuo */
    CHECK(attendi_stato(c, PH_COL_COLLEGATO, 3000));
    CHECK(ph_col_chiudi(c) == 0);
    nessun_oggetto_vivo();

    /* orfana nata alla chiusura */
    un_telefono();
    c = avvia(&o);
    CHECK(attendi_stato(c, PH_COL_COLLEGATO, 3000));
    PhColPrestito *p = ph_col_presta(c, NULL);
    CHECK(p != NULL);
    CHECK(ph_col_chiudi(c) == 1);
    prima = tre_conteggi();
    scarica_eventi();
    CHECK(tre_conteggi() == prima);
    ph_col_restituisci(p);
    nessun_oggetto_vivo();
}

static void prova_blocco_e_caduta_insieme(void) {
    puts("blocco e caduta quasi insieme: vale il tetto basso");
    un_telefono();
    PhColOpzioni o = veloci();
    o.riprova_iniziale_ms = 10;
    o.riprova_massimo_ms = 5000;
    o.riprova_dopo_blocco_ms = 40;
    o.controllo_ms = 50;
    PhCollegamento *c = avvia(&o);
    CHECK(attendi_stato(c, PH_COL_COLLEGATO, 3000));
    int prima = finto_collega_chiamate();
    finto_tel_blocca_e_cade(0); /* una sola operazione: il controllo non puo' vedere il blocco prima */
    CHECK(attendi_stato(c, PH_COL_PERSO, 3000));
    CHECK(ev_conta_stato(PH_COL_BLOCCATO) == 0); /* BLOCCATO non e' mai stato annunciato */
    PhColAvviso a;
    ph_col_stato(c, &a);
    CHECK(strstr(a.motivo, "sblocco") != NULL);
    CHECK(attendi_collega(prima + 12, 5000)); /* con il tetto basso bastano ~0,7 s */
    finto_tel_bloccato(0, false);
    finto_tel_acceso(0, true);
    CHECK(attendi_stato(c, PH_COL_COLLEGATO, 3000));
    CHECK(ph_col_chiudi(c) == 0);
    nessun_oggetto_vivo();
}

static atomic_int lento_finito, lento_entrato;
static _Atomic(PhColPrestito *) lento_prestito;
static atomic_int lento_attivo_letto;
static void su_evento_lento(uint32_t id, uint8_t che, uint16_t a, uint16_t b, void *u) {
    (void)id; (void)che; (void)a; (void)b; (void)u;
    atomic_store(&lento_entrato, 1);
    dormi(200);
    /* da dentro l'avviso si puo' chiamare ph_col_prestito_attivo (prende s->m):
     * non deve bloccarsi con la chiusura che aspetta questa funzione */
    PhColPrestito *p = atomic_load(&lento_prestito);
    if (p) atomic_store(&lento_attivo_letto, ph_col_prestito_attivo(p) ? 1 : 2);
    atomic_store(&lento_finito, 1);
}
static void *scarica_video(void *a) {
    (void)a;
    finto_tel_evento_video(0);
    return NULL;
}

static void prova_chiamata_in_corso_alla_chiusura(void) {
    puts("avviso lento in corso mentre si chiude con un'orfana");
    un_telefono();
    PhColOpzioni o = veloci();
    o.attesa_prestiti_ms = 30;
    o.evento_video = su_evento_lento;
    atomic_store(&lento_finito, 0);
    atomic_store(&lento_entrato, 0);
    atomic_store(&lento_attivo_letto, 0);
    PhCollegamento *c = avvia(&o);
    CHECK(attendi_stato(c, PH_COL_COLLEGATO, 3000));
    PhColPrestito *p = ph_col_presta(c, NULL);
    CHECK(p != NULL);
    atomic_store(&lento_prestito, p);
    pthread_t th;
    pthread_create(&th, NULL, scarica_video, NULL);
    long fine = ora_ms() + 3000; /* aspetta che la chiamata sia davvero dentro l'avviso */
    while (!atomic_load(&lento_entrato) && ora_ms() < fine) dormi(1);
    CHECK(atomic_load(&lento_entrato) == 1);
    CHECK(atomic_load(&lento_finito) == 0);
    CHECK(ph_col_chiudi(c) == 1);
    CHECK(atomic_load(&lento_attivo_letto) == 2); /* ha letto "non attiva": nessuno stallo */
    CHECK(atomic_load(&lento_finito) == 1); /* chiudi ha aspettato la chiamata in corso */
    pthread_join(th, NULL);
    ph_col_restituisci(p);
    nessun_oggetto_vivo();
}

static void prova_dati_rifiuta(void) {
    puts("dati rifiuta la chiusura (ricerca ancora aperta): componente non chiuso");
    char ord[32];
    PhColOpzioni o = veloci();
    o.attesa_prestiti_ms = 50;

    /* a) sessione orfana, prestito restituito con la ricerca aperta */
    un_telefono();
    PhCollegamento *c = avvia(&o);
    CHECK(attendi_stato(c, PH_COL_COLLEGATO, 3000));
    PhColPrestito *p = ph_col_presta(c, NULL);
    CHECK(p != NULL);
    finto_dati_rifiuta(true);
    finto_tel_acceso(0, false);
    CHECK(attendi_stato(c, PH_COL_PERSO, 3000));
    ph_col_restituisci(p);
    CHECK(finto_vivi_dati() == 1 && finto_vivi_componenti() == 1 && finto_vivi_adb() == 1);
    finto_ordine_rilascio(ord, sizeof ord);
    CHECK(strchr(ord, 'C') == NULL && strchr(ord, 'A') == NULL);
    finto_dati_rifiuta(false); /* la "ricerca" e' stata annullata */
    CHECK(ph_col_chiudi(c) == 0); /* ritenta e questa volta rilascia tutto */
    nessun_oggetto_vivo();
    finto_ordine_rilascio(ord, sizeof ord);
    CHECK(strcmp(ord, "DCA") == 0);

    /* b) nessun prestito: la caduta rilascia, dati rifiuta; si ritenta alla chiusura */
    un_telefono();
    c = avvia(&o);
    CHECK(attendi_stato(c, PH_COL_COLLEGATO, 3000));
    finto_dati_rifiuta(true);
    finto_tel_acceso(0, false);
    CHECK(attendi_stato(c, PH_COL_PERSO, 3000));
    CHECK(finto_vivi_componenti() == 1); /* non chiuso */
    CHECK(ph_col_chiudi(c) == 0);        /* rifiuta ancora: resta da parte, nessun crash */
    CHECK(finto_vivi_componenti() == 1 && finto_vivi_dati() == 1);
    finto_dati_rifiuta(false);
    PhColOpzioni o2 = veloci(); /* un nuovo collegamento ritenta i residui */
    finto_tel_acceso(0, true);
    c = avvia(&o2);
    CHECK(attendi_stato(c, PH_COL_COLLEGATO, 3000));
    CHECK(ph_col_chiudi(c) == 0);
    nessun_oggetto_vivo();
}

static void prova_varie(void) {
    PhColAvviso a;
    puts("comando di lettura del seriale che fallisce: si riprova, nessun ripiego");
    un_telefono();
    finto_shell_fallimenti(1);
    PhColOpzioni o = veloci();
    PhCollegamento *c = avvia(&o);
    CHECK(attendi_stato(c, PH_COL_COLLEGATO, 3000));
    ph_col_stato(c, &a);
    CHECK(strcmp(a.telefono.seriale, "R5CT0000000") == 0); /* quello vero, non aid-/modello- */
    FintoNoto n;
    CHECK(!finto_noti_cerca("modello-Modello-X", &n));
    CHECK(finto_collega_chiamate() >= 2);
    CHECK(ph_col_chiudi(c) == 0);
    nessun_oggetto_vivo();

    /* fallisce anche la seconda lettura (android_id): niente ripiego */
    un_telefono();
    finto_tel_seriale(0, "");
    finto_tel_android_id(0, "x1");
    finto_shell_fallimenti(0);
    c = avvia(&o);
    CHECK(attendi_stato(c, PH_COL_COLLEGATO, 3000));
    CHECK(ph_col_chiudi(c) == 0);

    puts("avvisi di stato del telefono e di notifiche inoltrati");
    un_telefono();
    o = veloci();
    o.stato_telefono = su_stato_tel;
    o.notifiche = su_notifiche;
    atomic_store(&n_stato_tel, 0);
    atomic_store(&n_notifiche, 0);
    c = avvia(&o);
    CHECK(attendi_stato(c, PH_COL_COLLEGATO, 3000));
    finto_tel_bloccato(0, true);
    CHECK(atomic_load(&n_stato_tel) == 1 && atomic_load(&ultimo_bloccato_tel) == 1);
    CHECK(attendi_stato(c, PH_COL_BLOCCATO, 3000));
    finto_tel_notifiche(0);
    CHECK(atomic_load(&n_notifiche) == 1);
    CHECK(ph_col_chiudi(c) == 0);
    nessun_oggetto_vivo();

    puts("seriale atteso di ripiego: il nome mDNS non lo smentisce");
    un_telefono();
    finto_tel_seriale(0, "");
    finto_tel_android_id(0, "x1");
    finto_tel_nome_mdns(0, "adb-QUALCOSA-AbCdEf");
    o = veloci();
    c = avvia(&o);
    CHECK(attendi_stato(c, PH_COL_COLLEGATO, 3000));
    ph_col_stato(c, &a);
    CHECK(strcmp(a.telefono.seriale, "aid-x1") == 0);
    finto_tel_acceso(0, false);
    CHECK(attendi_stato(c, PH_COL_PERSO, 3000));
    /* lo stesso telefono ricompare a un altro indirizzo: lo trova solo la scoperta */
    finto_tel(0, "10.0.0.9", 5555, "", "Modello-X");
    finto_tel_android_id(0, "x1");
    finto_tel_nome_mdns(0, "adb-QUALCOSA-AbCdEf");
    int l[] = {0};
    finto_scoperta_telefoni(l, 1);
    CHECK(attendi_stato(c, PH_COL_COLLEGATO, 3000));
    CHECK(ph_col_chiudi(c) == 0);
    nessun_oggetto_vivo();

    puts("tetto basso dopo una caduta da BLOCCATO");
    un_telefono();
    o = veloci();
    o.riprova_iniziale_ms = 10;
    o.riprova_massimo_ms = 5000;
    o.riprova_dopo_blocco_ms = 40;
    c = avvia(&o);
    CHECK(attendi_stato(c, PH_COL_COLLEGATO, 3000));
    finto_tel_bloccato(0, true);
    CHECK(attendi_stato(c, PH_COL_BLOCCATO, 3000));
    int prima = finto_collega_chiamate();
    finto_tel_acceso(0, false);
    CHECK(attendi_stato(c, PH_COL_PERSO, 3000));
    /* con tetto 40 bastano ~0,7 s per 12 tentativi; senza ne servirebbero ~20 s */
    CHECK(attendi_collega(prima + 12, 5000));
    finto_tel_bloccato(0, false);
    finto_tel_acceso(0, true);
    CHECK(attendi_stato(c, PH_COL_COLLEGATO, 3000));
    CHECK(ph_col_chiudi(c) == 0);
    nessun_oggetto_vivo();
}

#if defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__)
void __sanitizer_set_report_fd(void *fd);
#endif

int main(void) {
    alarm(120); /* uno stallo fa fallire la prova invece di lasciarla appesa */
    /* Su stderr non deve mai finire un indirizzo o un numero di serie: si
     * rimanda in un file e alla fine si cerca. I rapporti dei sanitizer
     * restano sul vero stderr (descrittore salvato). */
    FILE *err = tmpfile();
    if (err) {
#if defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__)
        int vero = dup(2);
        __sanitizer_set_report_fd((void *)(intptr_t)vero);
#endif
        dup2(fileno(err), 2);
    }

    prova_prima_connessione();
    prova_non_trovato_poi_comparso();
    prova_non_abbinato();
    prova_caduta_e_riconnessione();
    prova_riconnetti_ora();
    prova_blocco_senza_caduta();
    prova_samsung();
    prova_componente_muore();
    prova_chiusura_in_ogni_stato();
    prova_chiusura_a_meta_tentativo();
    prova_chiusura_dall_avviso();
    prova_prestiti_rilasciati();
    prova_prestiti_tardivi();
    prova_prestiti_non_restituiti();
    prova_piu_telefoni();
    prova_diretto_e_config();
    prova_numero_di_serie();
    prova_varie();
    prova_dati_rifiuta();
    prova_esito_abbinamento();
    prova_abbina_concorrente();
    prova_giro_abbinamento();
    prova_motivi_tradotti();
    prova_silenzio_dopo_residui();
    prova_blocco_e_caduta_insieme();
    prova_chiamata_in_corso_alla_chiusura();

    if (err) {
        fflush(stderr);
        rewind(err);
        char buf[4096];
        size_t n = fread(buf, 1, sizeof buf - 1, err);
        buf[n] = '\0';
        CHECK(strstr(buf, "10.0.0.") == NULL);
        CHECK(strstr(buf, "fe80") == NULL);
        CHECK(strstr(buf, "R5CT") == NULL);
        CHECK(strstr(buf, "SER0000") == NULL);
        if (errori) printf("stderr:\n%s\n", buf);
    }
    printf(errori ? "ERRORI: %d\n" : "tutto bene\n", errori);
    return errori ? 1 : 0;
}
