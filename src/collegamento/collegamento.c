/* Il collegamento: macchina a stati con un thread proprio. Il contratto e le
 * regole (stati, prestiti, concorrenza) sono in collegamento.h.
 *
 * Ordine dei lucchetti (mai al contrario): c->m, poi s->m. Gli avvisi verso
 * chi sta sopra si chiamano sempre SENZA lucchetti in mano.
 * Il thread e' l'unico che cambia stato, crea e distrugge le sessioni. */
#include "collegamento/collegamento.h"

#include <errno.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "abbinamento/abbinamento.h"
#include "comune/buffer.h"
#include "config/lingua.h"
#include "scoperta/scoperta.h"

#define MAX_CANDIDATI 16
#define LUN_CODICE 32
#define LUN_HOST 128

/* ---- Sessione: un collegamento riuscito e i suoi oggetti vivi --------------*/
typedef struct Sessione {
    pthread_mutex_t m;
    pthread_cond_t cv;          /* prestiti arrivati a zero */
    int prestiti;
    bool perso;                 /* in rilascio: niente piu' prestiti */
    bool orfana;                /* il thread non l'ha aspettata: la distrugge l'ultimo */
    PhAdb *adb;
    PhComponente *comp;
    PhDati *dati;
    PhChiave *chiave_propria;   /* passa qui dal collegamento se la sessione resta orfana */
    atomic_bool bloccato;       /* scritto dall'avviso di dati, letto dal thread */
    PhColTelefono tel;
    PhDatiAvvisoNotifiche notifiche;
    void *utente_notifiche;
    PhDatiAvvisoStato stato_tel;
    void *utente_stato_tel;
    bool rifiuto_detto;
    atomic_bool muta;           /* niente piu' inoltri a chi sta sopra */
    atomic_int in_corso;        /* funzioni di passaggio entrate e non ancora uscite */
    PhEvento evento_video;
    void *utente_video;
    struct Sessione *prossimo; /* lista dei residui */
} Sessione;

struct PhColPrestito {
    Sessione *s;
};

struct PhCollegamento {
    PhColOpzioni o;
    char *jar;
    char *percorso_chiave;

    pthread_mutex_t m;
    pthread_cond_t cv;          /* sveglia il thread: chiusura, riprova, abbina */
    pthread_t thread;
    pthread_t tid;
    bool tid_valido;

    /* sotto m */
    bool stop;
    bool riprova_ora;
    bool abbina_richiesta;
    bool abbina_in_corso;
    char abbina_host[LUN_HOST];
    uint16_t abbina_porta;
    char abbina_codice[LUN_CODICE];
    bool abbina_esplicito;      /* host e porta dati da chi sta sopra */
    PhColAvviso ultimo;         /* fotografia dell'ultimo avviso */
    Sessione *sess;
    bool orfana_in_chiusura;
    bool riprendendo;           /* "riconnetti ora" consumato in NON_ABBINATO, CERCO non ancora pubblicato */

    /* solo del thread */
    PhChiave *chiave;
    char seriale_atteso[PH_CFG_LUN_SERIALE]; /* vuoto finche' non c'e' stato un collegamento */
    char motivo_t[192];         /* perche' l'ultimo tentativo e' fallito */
    PhColEsitoAbbinamento esito_pendente; /* per il prossimo imposta (solo del thread) */
    bool azzera_in_corso;       /* il prossimo imposta azzera abbina_in_corso nella stessa sezione */
    char host_nonab[64];        /* host che ha risposto "non abbinato" (per trovare il suo abbinamento) */
    int trovati;
    bool arbitraria;
};

/* ---- Piccole utilita' --------------------------------------------------------*/
/* Copia troncando, sempre terminata (niente snprintf("%s") che a -O3 avvisa). */
static void copia(char *dst, size_t cap, const char *src) {
    size_t n = strlen(src);
    if (n >= cap) n = cap - 1;
    memcpy(dst, src, n);
    dst[n] = '\0';
}

static void formatta(char *dst, size_t cap, const char *fmt, ...)
    __attribute__((format(printf, 3, 4)));
static void formatta(char *dst, size_t cap, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(dst, cap, fmt, ap);
    va_end(ap);
    if (n < 0) dst[0] = '\0';
}

static void scadenza(struct timespec *t, int ms) {
    clock_gettime(CLOCK_MONOTONIC, t);
    t->tv_sec += ms / 1000;
    t->tv_nsec += (long)(ms % 1000) * 1000000L;
    if (t->tv_nsec >= 1000000000L) {
        t->tv_sec++;
        t->tv_nsec -= 1000000000L;
    }
}

static int predefinito(int v, int def) { return v > 0 ? v : def; }

/* ---- Sessione: creazione e distruzione -------------------------------------*/
static Sessione *sessione_nuova(void) {
    Sessione *s = calloc(1, sizeof *s);
    if (!s) return NULL;
    pthread_condattr_t at;
    pthread_condattr_init(&at);
    pthread_condattr_setclock(&at, CLOCK_MONOTONIC);
    pthread_mutex_init(&s->m, NULL);
    pthread_cond_init(&s->cv, &at);
    pthread_condattr_destroy(&at);
    atomic_init(&s->bloccato, false);
    atomic_init(&s->muta, false);
    atomic_init(&s->in_corso, 0);
    return s;
}

/* Sessioni la cui distruzione e' stata rifiutata da dati (ricerca ancora
 * aperta): restano qui, con la memoria valida e il componente NON chiuso (dati
 * e' ancora agganciato). Si ritenta la distruzione a ogni rilascio, a ogni
 * restituzione, a ogni ph_col_avvia e a ogni ph_col_chiudi. Se alla fine del
 * processo ce n'e' ancora una resta li' (raggiungibile, mai memoria liberata
 * male): i guardiani sul telefono ripristinano comunque. */
static pthread_mutex_t M_residui = PTHREAD_MUTEX_INITIALIZER;
static Sessione *residui;

/* Rilascio in ordine: prima chi sta sopra il componente (dati), poi il
 * componente (ripristina il telefono), per ultima la connessione.
 * Chi ha preso un prestito deve avere chiuso, prima di restituirlo, tutto cio'
 * che ha aperto con gli oggetti prestati (ricerche e appunti di dati, canali di
 * ph_componente_canale; i flussi video si possono chiudere anche dopo). Se una
 * ricerca e' rimasta aperta ph_dati_chiudi rifiuta (-1, nulla toccato): allora
 * NON si chiude il componente ne' la connessione, si scrive un messaggio
 * generico su stderr e la sessione passa ai residui. true = distrutta. */
/* Alza "muta" e aspetta le chiamate gia' in corso verso chi sta sopra. Le
 * funzioni di passaggio incrementano in_corso PRIMA di guardare muta (seq_cst):
 * o vedono muta e non chiamano, o sono contate qui e le si aspetta. Non
 * prendono mai lucchetti del collegamento, quindi si puo' aspettare anche con
 * s->m in mano. Non da dentro uno di quei callback (si aspetterebbe da se'). */
static void silenzia(Sessione *s) {
    atomic_store(&s->muta, true);
    while (atomic_load(&s->in_corso) > 0) {
        struct timespec t = {0, 1000000L};
        nanosleep(&t, NULL);
    }
}

static bool sessione_distruggi(Sessione *s) {
    if (s->dati) {
        PhErrore e = {{0}};
        if (ph_dati_chiudi(s->dati, &e) != 0) {
            if (!s->rifiuto_detto) {
                s->rifiuto_detto = true;
                fputs("collegamento: dati ha rifiutato la chiusura (ricerca ancora aperta?), "
                      "sessione tenuta da parte\n", stderr);
            }
            silenzia(s); /* da qui chi sta sopra non riceve piu' nulla */
            pthread_mutex_lock(&M_residui);
            s->prossimo = residui;
            residui = s;
            pthread_mutex_unlock(&M_residui);
            return false;
        }
        s->dati = NULL;
    }
    if (s->comp) ph_componente_chiudi(s->comp);
    if (s->adb) ph_adb_chiudi(s->adb);
    if (s->chiave_propria) ph_chiave_libera(s->chiave_propria);
    pthread_cond_destroy(&s->cv);
    pthread_mutex_destroy(&s->m);
    free(s);
    return true;
}

static void riprova_residui(void) {
    pthread_mutex_lock(&M_residui);
    Sessione *l = residui;
    residui = NULL;
    pthread_mutex_unlock(&M_residui);
    while (l) {
        Sessione *n = l->prossimo;
        l->prossimo = NULL;
        (void)sessione_distruggi(l); /* se rifiuta ancora, torna fra i residui */
        l = n;
    }
}

/* Chiamati dal thread interno del componente: solo una scrittura atomica, il
 * thread del collegamento la legge al controllo periodico. Non toccano il
 * collegamento (che potrebbe non esistere piu' se la sessione e' orfana). */
static void avviso_stato_dati(const PhDatiStato *st, void *utente) {
    Sessione *s = utente;
    atomic_store(&s->bloccato, st->bloccato);
    /* batteria, rete: li vuole chi sta sopra, ma non dopo il silenzio */
    atomic_fetch_add(&s->in_corso, 1);
    if (s->stato_tel && !atomic_load(&s->muta)) s->stato_tel(st, s->utente_stato_tel);
    atomic_fetch_sub(&s->in_corso, 1);
}
static void avviso_notifiche_dati(void *utente) {
    Sessione *s = utente;
    atomic_fetch_add(&s->in_corso, 1);
    if (s->notifiche && !atomic_load(&s->muta)) s->notifiche(s->utente_notifiche);
    atomic_fetch_sub(&s->in_corso, 1);
}
static void avviso_evento_video(uint32_t id, uint8_t che, uint16_t a, uint16_t b, void *utente) {
    Sessione *s = utente;
    atomic_fetch_add(&s->in_corso, 1);
    if (s->evento_video && !atomic_load(&s->muta)) s->evento_video(id, che, a, b, s->utente_video);
    atomic_fetch_sub(&s->in_corso, 1);
}

/* ---- Avvisi -------------------------------------------------------------------*/
/* Cambia lo stato e avvisa. Non ripete un avviso identico (stesso stato, stesso
 * motivo) salvo forza. Solo il thread lo chiama: gli avvisi escono in ordine. */
static void imposta(PhCollegamento *c, PhColStato st, const char *motivo, bool forza,
                    const PhColTelefono *tel) {
    PhColAvviso a;
    pthread_mutex_lock(&c->m);
    if (!forza && c->ultimo.stato == st && strcmp(c->ultimo.motivo, motivo) == 0) {
        pthread_mutex_unlock(&c->m);
        return;
    }
    memset(&c->ultimo, 0, sizeof c->ultimo);
    c->ultimo.stato = st;
    copia(c->ultimo.motivo, sizeof c->ultimo.motivo, motivo);
    c->ultimo.esito_abbinamento = c->esito_pendente;
    c->esito_pendente = PH_COL_ABB_NESSUNO;
    if (c->azzera_in_corso) {
        /* La richiesta si libera NELLA STESSA sezione che pubblica il nuovo stato e
         * l'esito: fra le due cose non esiste un istante in cui ph_col_abbina sia
         * accettata e nessuno la serva. */
        c->abbina_in_corso = false;
        c->azzera_in_corso = false;
    }
    if (tel) {
        c->ultimo.ha_telefono = true;
        c->ultimo.telefono = *tel;
        c->ultimo.telefoni_trovati = c->trovati;
        c->ultimo.scelta_arbitraria = c->arbitraria;
    }
    a = c->ultimo;
    pthread_mutex_unlock(&c->m);
    if (c->o.avviso) c->o.avviso(&a, c->o.utente);
}

/* ---- Attese interrompibili ---------------------------------------------------*/
/* Aspetta ms (< 0 = senza limite) o finche' qualcuno sveglia il thread
 * (chiusura, "riconnetti ora", richiesta di abbinamento). */
static void attendi(PhCollegamento *c, int ms) {
    struct timespec t;
    if (ms >= 0) scadenza(&t, ms);
    pthread_mutex_lock(&c->m);
    /* Una richiesta di abbinamento sveglia solo in NON_ABBINATO: in ogni altro stato non
     * si puo' servire, e svegliarsi per lei farebbe girare il thread a vuoto. */
    while (!c->stop && !c->riprova_ora &&
           !(c->abbina_richiesta && c->ultimo.stato == PH_COL_NON_ABBINATO)) {
        if (ms < 0) {
            pthread_cond_wait(&c->cv, &c->m);
        } else if (pthread_cond_timedwait(&c->cv, &c->m, &t) == ETIMEDOUT) {
            break;
        }
    }
    pthread_mutex_unlock(&c->m);
}

static bool fermati(PhCollegamento *c) {
    pthread_mutex_lock(&c->m);
    bool r = c->stop;
    pthread_mutex_unlock(&c->m);
    return r;
}

static bool consuma_riprova(PhCollegamento *c) {
    pthread_mutex_lock(&c->m);
    bool r = c->riprova_ora;
    c->riprova_ora = false;
    pthread_mutex_unlock(&c->m);
    return r;
}

/* ---- Numero di serie e nome ----------------------------------------------------*/
/* Tiene [A-Za-z0-9._-], il resto diventa '_' (il valore arriva dal telefono). */
static void pulisci(const char *src, size_t n, char *dst, size_t cap) {
    size_t j = 0;
    for (size_t i = 0; i < n && j + 1 < cap; i++) {
        unsigned char ch = (unsigned char)src[i];
        bool ok = (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
                  (ch >= '0' && ch <= '9') || ch == '.' || ch == '_' || ch == '-';
        dst[j++] = ok ? (char)ch : '_';
    }
    dst[j] = '\0';
}

/* Esegue un comando e ne ricava la prima riga ripulita. Torna -1 se il comando
 * NON si e' potuto eseguire (tempo scaduto, canale rifiutato: un intoppo che
 * si riprova), 0 altrimenti; con 0 la riga puo' essere vuota (valore assente,
 * oppure codice di uscita diverso da zero: si usa il ripiego). */
static int riga_di(PhAdb *a, const char *cmd, char *dst, size_t cap) {
    PhBuffer out = PH_BUFFER_VUOTO;
    PhErrore e = {{0}};
    int codice = 0;
    dst[0] = '\0';
    if (ph_shell_esegui(a, cmd, &out, NULL, &codice, 3000, &e) != 0) {
        ph_buffer_libera(&out);
        return -1;
    }
    if (codice == 0 && out.dati) {
        size_t n = 0;
        while (n < out.lunghezza && out.dati[n] != '\n' && out.dati[n] != '\r') n++;
        while (n > 0 && out.dati[n - 1] == ' ') n--;
        pulisci((const char *)out.dati, n, dst, cap);
    }
    ph_buffer_libera(&out);
    return 0;
}

/* Modello dal banner ("...;ro.product.model=XXX;..."), ripulito; vuoto se manca. */
static void modello_da_banner(PhAdb *a, char *dst, size_t cap) {
    static const char chiave[] = "ro.product.model=";
    const char *b = ph_adb_presentazione(a);
    dst[0] = '\0';
    if (!b) return;
    const char *p = strstr(b, chiave);
    if (!p) return;
    p += sizeof chiave - 1;
    size_t n = strcspn(p, ";:");
    pulisci(p, n, dst, cap);
}

/* 0 = seriale (o ripiego) scritto in dst, -1 = un comando e' fallito: il
 * tentativo fallisce e si riprova (mai un ripiego per un intoppo momentaneo). */
static int leggi_seriale(PhAdb *a, char *dst, size_t cap) {
    char v[48];
    if (riga_di(a, "getprop ro.serialno", v, sizeof v) != 0) return -1;
    if (v[0] && strcmp(v, "unknown") != 0) {
        copia(dst, cap, v);
        return 0;
    }
    if (riga_di(a, "settings get secure android_id", v, sizeof v) != 0) return -1;
    if (v[0] && strcmp(v, "null") != 0) {
        formatta(dst, cap, "aid-%s", v);
        return 0;
    }
    modello_da_banner(a, v, sizeof v);
    formatta(dst, cap, "modello-%s", v[0] ? v : "sconosciuto");
    return 0;
}

/* "adb-<seriale>-<suffisso>" (nome mDNS): ne ricava il seriale, vuoto se non e' cosi'. */
static void seriale_da_nome(const char *nome, char *dst, size_t cap) {
    dst[0] = '\0';
    if (strncmp(nome, "adb-", 4) != 0) return;
    const char *r = nome + 4;
    const char *ultimo = strrchr(r, '-');
    if (!ultimo || ultimo == r) return;
    pulisci(r, (size_t)(ultimo - r), dst, cap);
}

/* "host:porta" o "[host]:porta" -> host e porta. false se non e' in quella forma. */
static bool leggi_indirizzo(const char *testo, char *host, size_t cap, uint16_t *porta) {
    const char *dp;
    const char *h;
    size_t n;
    if (testo[0] == '[') {
        const char *fine = strchr(testo, ']');
        if (!fine || fine[1] != ':') return false;
        h = testo + 1;
        n = (size_t)(fine - h);
        dp = fine + 1;
    } else {
        dp = strrchr(testo, ':');
        if (!dp) return false;
        h = testo;
        n = (size_t)(dp - testo);
        if (memchr(h, ':', n)) return false; /* IPv6 senza parentesi: ambiguo */
    }
    if (n == 0 || n >= cap) return false;
    char *fine_num = NULL;
    errno = 0;
    unsigned long p = strtoul(dp + 1, &fine_num, 10);
    if (errno != 0 || fine_num == dp + 1 || *fine_num != '\0' || p == 0 || p > 65535) return false;
    memcpy(host, h, n);
    host[n] = '\0';
    *porta = (uint16_t)p;
    return true;
}

/* ---- Un tentativo di collegamento ------------------------------------------------*/
typedef enum { T_OK, T_PROSSIMO, T_FERMA, T_NON_ABBINATO } Esito;

typedef struct {
    char host[64];
    uint16_t porta;
    char seriale[PH_CFG_LUN_SERIALE]; /* ricavato dal nome mDNS, puo' essere vuoto */
    bool noto;
    int64_t visto;
    int64_t peso;
    int ordine;
} Candidato;

static int confronta(const void *x, const void *y) {
    const Candidato *a = x, *b = y;
    if (a->peso != b->peso) return a->peso < b->peso ? 1 : -1;
    return a->ordine - b->ordine;
}

/* Collega un candidato per intero: adb, serial, componente, dati. Su T_OK
 * *fuori e' la sessione pronta (non ancora pubblicata). */
static Esito prova_candidato(PhCollegamento *c, const char *host, uint16_t porta, Sessione **fuori) {
    PhErrore e = {{0}};
    bool nonab = false;
    PhAdb *adb = ph_adb_collega(host, porta, c->chiave, c->o.timeout_collega_ms, &nonab, &e);
    if (!adb) {
        if (nonab) return T_NON_ABBINATO;
        copia(c->motivo_t, sizeof c->motivo_t, ph_t("collegamento al telefono non riuscito"));
        return T_PROSSIMO;
    }
    Sessione *s = sessione_nuova();
    if (!s) {
        ph_adb_chiudi(adb);
        copia(c->motivo_t, sizeof c->motivo_t, ph_t("memoria finita"));
        return T_FERMA;
    }
    s->adb = adb;
    if (fermati(c)) {
        sessione_distruggi(s);
        return T_FERMA;
    }
    if (leggi_seriale(adb, s->tel.seriale, sizeof s->tel.seriale) != 0) {
        sessione_distruggi(s);
        copia(c->motivo_t, sizeof c->motivo_t, ph_t("lettura dei dati del telefono non riuscita"));
        return T_PROSSIMO;
    }
    if (c->seriale_atteso[0] && strcmp(s->tel.seriale, c->seriale_atteso) != 0) {
        sessione_distruggi(s);
        copia(c->motivo_t, sizeof c->motivo_t, ph_t("in rete c'e' un altro telefono, non quello di prima"));
        return T_PROSSIMO;
    }
    if (fermati(c)) {
        sessione_distruggi(s);
        return T_FERMA;
    }
    PhTelefonoInfo info;
    memset(&info, 0, sizeof info);
    s->evento_video = c->o.evento_video;
    s->utente_video = c->o.utente_video;
    s->comp = ph_componente_avvia(adb, c->jar, c->o.evento_video ? avviso_evento_video : NULL, s, &info,
                                  c->o.timeout_componente_ms, &e);
    if (!s->comp) {
        /* il testo di e puo' contenere indirizzi: l'avviso usa frasi proprie */
        copia(c->motivo_t, sizeof c->motivo_t, ph_t("avvio del componente sul telefono non riuscito"));
        sessione_distruggi(s);
        return T_FERMA;
    }
    s->notifiche = c->o.notifiche;
    s->utente_notifiche = c->o.utente_notifiche;
    s->stato_tel = c->o.stato_telefono;
    s->utente_stato_tel = c->o.utente_stato_telefono;
    PhDatiAvvisi av = {.stato = avviso_stato_dati,
                       .notifiche = c->o.notifiche ? avviso_notifiche_dati : NULL,
                       .utente = s};
    s->dati = ph_dati_apri(s->comp, &av, &e);
    if (!s->dati) {
        copia(c->motivo_t, sizeof c->motivo_t, ph_t("dati del telefono non disponibili"));
        sessione_distruggi(s);
        return T_FERMA;
    }
    PhDatiStato st;
    memset(&st, 0, sizeof st);
    if (ph_dati_stato(s->dati, &st, &e) == 0) atomic_store(&s->bloccato, st.bloccato);

    copia(s->tel.indirizzo, sizeof s->tel.indirizzo, host);
    s->tel.porta = porta;
    s->tel.info = info;
    if (info.modello[0]) {
        copia(s->tel.nome, sizeof s->tel.nome, info.modello);
    } else {
        char m[PH_CFG_LUN_NOME];
        modello_da_banner(adb, m, sizeof m);
        copia(s->tel.nome, sizeof s->tel.nome, m);
    }
    *fuori = s;
    return T_OK;
}

/* Aggiorna telefoni.toml dopo un collegamento riuscito. Un errore non e' mai fatale. */
static void registra_telefono(PhCollegamento *c, PhCfgTelefoni *noti, bool puo_salvare, Sessione *s) {
    (void)c;
    PhCfgTelefonoNoto *v = NULL;
    for (size_t i = 0; i < noti->n; i++)
        if (strcmp(noti->voci[i].seriale, s->tel.seriale) == 0) v = &noti->voci[i];
    s->tel.conosciuto = v != NULL;
    if (!puo_salvare) return;
    if (!v) {
        if (noti->n < PH_CFG_MAX_TELEFONI) {
            v = &noti->voci[noti->n++];
        } else { /* pieno: si rimpiazza il meno recente */
            v = &noti->voci[0];
            for (size_t i = 1; i < noti->n; i++)
                if (noti->voci[i].visto < v->visto) v = &noti->voci[i];
        }
        memset(v, 0, sizeof *v);
    }
    copia(v->seriale, sizeof v->seriale, s->tel.seriale);
    copia(v->nome, sizeof v->nome, s->tel.nome);
    if (strchr(s->tel.indirizzo, ':'))
        formatta(v->indirizzo, sizeof v->indirizzo, "[%s]:%u", s->tel.indirizzo,
                 (unsigned)s->tel.porta);
    else
        formatta(v->indirizzo, sizeof v->indirizzo, "%s:%u", s->tel.indirizzo, (unsigned)s->tel.porta);
    v->visto = (int64_t)time(NULL);
    PhErrore e = {{0}};
    (void)ph_cfg_telefoni_salva(noti, &e);
}

/* Un giro completo: ultimo indirizzo del telefono noto, poi la scoperta. */
static Esito tentativo(PhCollegamento *c, Sessione **fuori) {
    PhErrore e = {{0}};
    Esito es = T_PROSSIMO;
    c->trovati = 0;
    c->arbitraria = false;
    c->host_nonab[0] = '\0';
    bool nonab = false; /* qualcuno ha risposto "non abbinato": conta solo se nessuno riesce */
    copia(c->motivo_t, sizeof c->motivo_t, ph_t("nessun telefono trovato in rete"));

    if (!c->chiave) {
        char perc[PH_CFG_LUN_PERCORSO];
        const char *p = c->percorso_chiave;
        if (!p) {
            if (ph_cfg_percorso_chiave(perc, sizeof perc, &e) != 0) {
                copia(c->motivo_t, sizeof c->motivo_t, ph_t("percorso della chiave del PC non disponibile"));
                return T_PROSSIMO;
            }
            p = perc;
        }
        c->chiave = ph_chiave_carica_o_crea(p, &e);
        if (!c->chiave) {
            copia(c->motivo_t, sizeof c->motivo_t, ph_t("chiave del PC non disponibile"));
            return T_PROSSIMO;
        }
    }

    PhCfgTelefoni *noti = calloc(1, sizeof *noti);
    if (!noti) {
        copia(c->motivo_t, sizeof c->motivo_t, ph_t("memoria finita"));
        return T_FERMA;
    }
    /* Dopo -1 non si salva (il file non e' stato ne' letto ne' messo da parte). */
    bool puo_salvare = ph_cfg_telefoni_carica(noti, &e) != -1;
    if (!puo_salvare) ph_cfg_telefoni_predefiniti(noti);

    char host_diretto[64] = "";
    uint16_t porta_diretta = 0;
    bool diretto = false;

    /* 1. ultimo indirizzo del telefono atteso (o del piu' recente) */
    const PhCfgTelefonoNoto *scelto = NULL;
    for (size_t i = 0; i < noti->n; i++) {
        const PhCfgTelefonoNoto *v = &noti->voci[i];
        if (c->seriale_atteso[0]) {
            if (strcmp(v->seriale, c->seriale_atteso) == 0) scelto = v;
        } else if (!scelto || v->visto > scelto->visto) {
            scelto = v;
        }
    }
    if (scelto && leggi_indirizzo(scelto->indirizzo, host_diretto, sizeof host_diretto, &porta_diretta)) {
        diretto = true;
        es = prova_candidato(c, host_diretto, porta_diretta, fuori);
        if (es == T_NON_ABBINATO) {
            nonab = true;
            copia(c->host_nonab, sizeof c->host_nonab, host_diretto);
            es = T_PROSSIMO;
        }
        if (es != T_PROSSIMO) goto fine;
    }
    if (fermati(c)) {
        es = T_FERMA;
        goto fine;
    }

    /* 2. la scoperta in rete */
    PhTelefonoTrovato trovati[MAX_CANDIDATI];
    int n = ph_scoperta_cerca(c->o.durata_scoperta_ms, trovati, MAX_CANDIDATI, &e);
    if (n < 0) {
        copia(c->motivo_t, sizeof c->motivo_t, ph_t("ricerca in rete non riuscita"));
        es = T_PROSSIMO;
        goto fine;
    }
    Candidato cand[MAX_CANDIDATI];
    int nc = 0;
    bool qualcuno_noto = false;
    int visti = 0; /* telefoni in collegamento, senza doppioni sull'indirizzo gia' provato */
    for (int i = 0; i < n && nc < MAX_CANDIDATI; i++) {
        if (trovati[i].abbinamento) continue;
        visti++;
        if (diretto && trovati[i].porta == porta_diretta &&
            strcmp(trovati[i].indirizzo, host_diretto) == 0)
            continue;
        Candidato *k = &cand[nc];
        memset(k, 0, sizeof *k);
        copia(k->host, sizeof k->host, trovati[i].indirizzo);
        k->porta = trovati[i].porta;
        seriale_da_nome(trovati[i].nome, k->seriale, sizeof k->seriale);
        k->ordine = nc;
        for (size_t j = 0; j < noti->n; j++) {
            const PhCfgTelefonoNoto *v = &noti->voci[j];
            bool stesso = (k->seriale[0] && strcmp(v->seriale, k->seriale) == 0);
            char h[64];
            uint16_t p;
            if (!stesso && leggi_indirizzo(v->indirizzo, h, sizeof h, &p) && strcmp(h, k->host) == 0)
                stesso = true;
            if (stesso && (!k->noto || v->visto > k->visto)) {
                k->noto = true;
                k->visto = v->visto;
            }
        }
        if (c->seriale_atteso[0]) {
            /* ricollegamento: solo lo stesso telefono (o uno di cui il nome non dice nulla,
             * che si controlla dopo il collegamento con il numero di serie) */
            /* se il seriale atteso e' un ripiego (aid-, modello-) il nome mDNS, che
             * porta il seriale vero, non dice nulla: vale come ignoto */
            bool ripiego = strncmp(c->seriale_atteso, "aid-", 4) == 0 ||
                           strncmp(c->seriale_atteso, "modello-", 8) == 0;
            if (k->seriale[0] && !ripiego)
                k->peso = strcmp(k->seriale, c->seriale_atteso) == 0 ? 2 : 0;
            else
                k->peso = 1;
            if (k->peso == 0) continue;
        } else {
            k->peso = k->noto ? ((int64_t)1 << 40) + k->visto : 0;
        }
        if (k->noto) qualcuno_noto = true;
        nc++;
    }
    if (nc == 0) {
        if (c->seriale_atteso[0] && visti > 0)
            copia(c->motivo_t, sizeof c->motivo_t, ph_t("il telefono di prima non si trova, in rete c'e' un altro"));
        es = T_PROSSIMO;
        goto fine;
    }
    qsort(cand, (size_t)nc, sizeof cand[0], confronta);
    if (!c->seriale_atteso[0]) {
        c->trovati = visti;
        c->arbitraria = visti > 1 && !qualcuno_noto;
    }
    es = T_PROSSIMO;
    for (int i = 0; i < nc; i++) {
        if (fermati(c)) {
            es = T_FERMA;
            break;
        }
        es = prova_candidato(c, cand[i].host, cand[i].porta, fuori);
        if (es == T_NON_ABBINATO) {
            nonab = true;
            if (!c->host_nonab[0]) copia(c->host_nonab, sizeof c->host_nonab, cand[i].host);
            es = T_PROSSIMO;
            continue;
        }
        if (es != T_PROSSIMO) break;
    }

fine:
    if (es == T_PROSSIMO && nonab) es = T_NON_ABBINATO;
    if (es == T_OK) registra_telefono(c, noti, puo_salvare, *fuori);
    free(noti);
    return es;
}

/* ---- Rilascio di una sessione ---------------------------------------------------*/
/* Toglie la sessione dal collegamento (niente piu' prestiti), chiede di
 * restituire, aspetta, distrugge o lascia orfana. true = orfana. */
static bool rilascia(PhCollegamento *c) {
    pthread_mutex_lock(&c->m);
    Sessione *s = c->sess;
    c->sess = NULL;
    pthread_mutex_unlock(&c->m);
    riprova_residui();
    if (!s) return false;

    pthread_mutex_lock(&s->m);
    s->perso = true;
    pthread_mutex_unlock(&s->m);

    if (c->o.in_perdita) c->o.in_perdita(c->o.utente);

    struct timespec t;
    scadenza(&t, c->o.attesa_prestiti_ms);
    pthread_mutex_lock(&s->m);
    while (s->prestiti > 0) {
        if (pthread_cond_timedwait(&s->cv, &s->m, &t) == ETIMEDOUT) break;
    }
    if (s->prestiti > 0) {
        /* Si silenzia SENZA s->m: una funzione di avviso puo' chiamare
         * ph_col_prestito_attivo (che lo prende) e si bloccherebbe per sempre.
         * Finche' orfana e' falso nessuna restituzione distrugge la sessione. */
        pthread_mutex_unlock(&s->m);
        silenzia(s);
        pthread_mutex_lock(&s->m);
    }
    bool libera = s->prestiti == 0; /* ricalcolato: qualcuno puo' aver restituito */
    /* La chiave puo' essere ancora riferita dalla connessione, e la distruzione
     * puo' slittare (orfana, o rifiutata da dati): la libera la sessione. Il
     * collegamento ne carica una nuova al prossimo tentativo. */
    if (c->chiave) {
        s->chiave_propria = c->chiave;
        c->chiave = NULL;
    }
    /* Un'orfana non inoltra nulla (una sessione caduta non ha eventi utili, e
     * alla chiusura chi sta sopra libera i suoi dati appena torna ph_col_chiudi):
     * muta e' gia' alzata e le chiamate in corso sono finite (vedi sopra). */
    if (!libera) s->orfana = true;
    pthread_mutex_unlock(&s->m);
    if (libera) (void)sessione_distruggi(s);
    return !libera;
}

/* ---- Controllo periodico in COLLEGATO / BLOCCATO ------------------------------------*/
/* Torna quando la sessione e' caduta (motivo riempito) o c'e' la chiusura. */
static void monitora(PhCollegamento *c, Sessione *s, bool bloccato_ora, char *motivo, size_t cap,
                     bool *da_bloccato) {
    /* bloccato_ora e' lo stato GIA' annunciato: se il telefono si blocca fra
     * l'avviso e il primo controllo, la differenza si vede e BLOCCATO esce. */
    *da_bloccato = false;
    for (;;) {
        if (fermati(c)) return;
        (void)consuma_riprova(c);
        if (!ph_adb_viva(s->adb)) {
            /* blocco e caduta del debug wireless possono arrivare quasi insieme:
             * conta anche il flag non ancora annunciato */
            *da_bloccato = bloccato_ora || atomic_load(&s->bloccato);
            if (*da_bloccato)
                copia(motivo, cap, ph_t("il telefono si e' bloccato e la connessione e' caduta: riprovo allo sblocco"));
            else
                copia(motivo, cap, ph_t("connessione con il telefono caduta"));
            return;
        }
        if (!ph_componente_vivo(s->comp)) {
            *da_bloccato = bloccato_ora || atomic_load(&s->bloccato);
            copia(motivo, cap, ph_t("il componente sul telefono non risponde piu'"));
            return;
        }
        bool b = atomic_load(&s->bloccato);
        if (b != bloccato_ora) {
            bloccato_ora = b;
            imposta(c, b ? PH_COL_BLOCCATO : PH_COL_COLLEGATO,
                    b ? ph_t("il telefono e' bloccato") : ph_t("il telefono e' stato sbloccato"), false, &s->tel);
        }
        attendi(c, c->o.controllo_ms);
    }
}

/* ---- Abbinamento ------------------------------------------------------------------*/
/* Cerca in rete il servizio di abbinamento (esiste solo mentre il telefono
 * mostra "Associa dispositivo con codice"): quello dello stesso telefono che
 * ha risposto "non abbinato", o l'unico presente. */
static bool trova_abbinamento(PhCollegamento *c, char *host, size_t cap, uint16_t *porta) {
    PhTelefonoTrovato t[MAX_CANDIDATI];
    PhErrore e = {{0}};
    int n = ph_scoperta_cerca(c->o.durata_scoperta_ms, t, MAX_CANDIDATI, &e);
    int tot = 0, ultimo = -1, stesso = -1;
    for (int i = 0; i < n; i++) {
        if (!t[i].abbinamento) continue;
        tot++;
        ultimo = i;
        if (c->host_nonab[0] && strcmp(t[i].indirizzo, c->host_nonab) == 0) stesso = i;
    }
    int k = stesso >= 0 ? stesso : (tot == 1 ? ultimo : -1);
    if (k < 0) return false;
    copia(host, cap, t[k].indirizzo);
    *porta = t[k].porta;
    return true;
}

static void gestisci_non_abbinato(PhCollegamento *c) {
    attendi(c, -1);
    pthread_mutex_lock(&c->m);
    bool stop = c->stop;
    bool abbina = c->abbina_richiesta;
    bool esplicito = c->abbina_esplicito;
    bool riprova = c->riprova_ora;
    char host[LUN_HOST], codice[LUN_CODICE];
    uint16_t porta = c->abbina_porta;
    copia(host, sizeof host, c->abbina_host);
    copia(codice, sizeof codice, c->abbina_codice);
    if (abbina) {
        c->abbina_richiesta = false;
        c->abbina_in_corso = true;
        explicit_bzero(c->abbina_codice, sizeof c->abbina_codice);
    }
    if (riprova && !abbina) {
        c->riprova_ora = false;
        c->riprendendo = true; /* fino alla pubblicazione di CERCO nessun abbinamento si accetta */
    }
    pthread_mutex_unlock(&c->m);
    if (stop) goto fine;
    if (abbina) {
        PhErrore e = {{0}};
        char m[192];
        bool riuscito = false;
        if (!c->chiave) {
            char perc[PH_CFG_LUN_PERCORSO];
            const char *p = c->percorso_chiave;
            if (!p && ph_cfg_percorso_chiave(perc, sizeof perc, &e) == 0) p = perc;
            if (p) c->chiave = ph_chiave_carica_o_crea(p, &e);
        }
        if (!c->chiave) {
            copia(m, sizeof m, ph_t("chiave del PC non disponibile"));
        } else if (!esplicito && !trova_abbinamento(c, host, sizeof host, &porta)) {
            copia(m, sizeof m,
                  ph_t("apri sul telefono la schermata \"Associa dispositivo con codice\" e riprova"));
        } else if (ph_abbinamento_esegui(host, porta, codice, c->chiave,
                                         c->o.timeout_abbinamento_ms, &e) == 0) {
            riuscito = true;
            copia(m, sizeof m, ph_t("abbinamento riuscito: mi collego"));
        } else {
            /* il testo di e puo' contenere l'indirizzo: frase propria */
            copia(m, sizeof m, ph_t("abbinamento non riuscito: controlla il codice e riprova"));
        }
        explicit_bzero(codice, sizeof codice);
        abbina = false;
        if (!fermati(c)) {
            /* abbina_in_corso si azzera dentro imposta, insieme alla pubblicazione:
             * chi riceve l'avviso di un fallimento puo' subito richiamare ph_col_abbina. */
            c->esito_pendente = riuscito ? PH_COL_ABB_RIUSCITO : PH_COL_ABB_FALLITO;
            c->azzera_in_corso = true;
            imposta(c, riuscito ? PH_COL_CERCO : PH_COL_NON_ABBINATO, m, true, NULL);
        } else {
            /* Chiusura arrivata durante l'abbinamento: nessun avviso lo conclude. */
            pthread_mutex_lock(&c->m);
            c->abbina_in_corso = false;
            pthread_mutex_unlock(&c->m);
        }
    } else if (riprova) {
        imposta(c, PH_COL_CERCO, ph_t("riprovo a collegarmi"), false, NULL);
        pthread_mutex_lock(&c->m);
        c->riprendendo = false;
        pthread_mutex_unlock(&c->m);
    }
fine:
    explicit_bzero(codice, sizeof codice);
    if (abbina) { /* uscito per la chiusura con una richiesta presa: la si libera */
        pthread_mutex_lock(&c->m);
        c->abbina_in_corso = false;
        pthread_mutex_unlock(&c->m);
    }
}

/* ---- Il thread ------------------------------------------------------------------------*/
static void *thread_principale(void *arg) {
    PhCollegamento *c = arg;
    pthread_mutex_lock(&c->m);
    c->tid = pthread_self();
    c->tid_valido = true;
    pthread_mutex_unlock(&c->m);

    imposta(c, PH_COL_CERCO, ph_t("ricerca del telefono in rete"), true, NULL);
    int backoff = c->o.riprova_iniziale_ms;
    int tetto = c->o.riprova_massimo_ms;

    while (!fermati(c)) {
        riprova_residui(); /* sul thread del collegamento: la restituzione resta immediata */
        pthread_mutex_lock(&c->m);
        PhColStato stato = c->ultimo.stato;
        bool vecchia = c->abbina_richiesta && stato != PH_COL_NON_ABBINATO;
        if (vecchia) { /* non servibile in questo stato: la si conclude, mai lasciata appesa */
            c->abbina_richiesta = false;
            explicit_bzero(c->abbina_codice, sizeof c->abbina_codice);
        }
        pthread_mutex_unlock(&c->m);
        if (vecchia) {
            c->esito_pendente = PH_COL_ABB_FALLITO;
            imposta(c, stato, ph_t("abbinamento non eseguito: il telefono non lo aspettava piu'"), true,
                    NULL);
            continue;
        }

        if (stato == PH_COL_NON_ABBINATO) {
            gestisci_non_abbinato(c);
            continue;
        }
        if (consuma_riprova(c)) backoff = c->o.riprova_iniziale_ms;

        Sessione *s = NULL;
        Esito es = tentativo(c, &s);
        if (es == T_OK) {
            backoff = c->o.riprova_iniziale_ms;
            tetto = c->o.riprova_massimo_ms;
            pthread_mutex_lock(&c->m);
            c->sess = s;
            bool stop = c->stop;
            pthread_mutex_unlock(&c->m);
            copia(c->seriale_atteso, sizeof c->seriale_atteso, s->tel.seriale);
            bool bl = atomic_load(&s->bloccato); /* letto una volta: e' lo stato annunciato */
            if (!stop) {
                char m[192];
                copia(m, sizeof m, ph_t("collegato"));
                if (bl) {
                    copia(m, sizeof m, ph_t("collegato, ma il telefono e' bloccato"));
                } else if (c->arbitraria) {
                    char num[16];
                    formatta(num, sizeof num, "%d", c->trovati);
                    ph_lingua_componi(m, sizeof m,
                                      ph_t("collegato: trovati {numero} telefoni e nessuno "
                                           "conosciuto, scelto il primo"),
                                      "numero", num, (const char *)NULL);
                }
                imposta(c, bl ? PH_COL_BLOCCATO : PH_COL_COLLEGATO, m, false, &s->tel);
            }
            char motivo[192] = "";
            bool da_bloccato = false;
            monitora(c, s, bl, motivo, sizeof motivo, &da_bloccato);
            /* Caduta da BLOCCATO (telefoni che spengono il debug wireless al blocco):
             * la riconnessione e' attesa allo sblocco, il tetto e' basso. */
            if (da_bloccato && c->o.riprova_dopo_blocco_ms < tetto) tetto = c->o.riprova_dopo_blocco_ms;
            if (backoff > tetto) backoff = tetto;
            bool orf = rilascia(c);
            if (fermati(c)) {
                if (orf) {
                    pthread_mutex_lock(&c->m);
                    c->orfana_in_chiusura = true;
                    pthread_mutex_unlock(&c->m);
                }
            } else {
                imposta(c, PH_COL_PERSO, motivo, false, NULL);
            }
            continue;
        }
        if (es == T_NON_ABBINATO) {
            /* Un "riconnetti ora" arrivato durante il tentativo e' assorbito qui; uno
             * arrivato dopo questa riga e prima della pubblicazione di NON_ABBINATO resta
             * valido e porta a un nuovo tentativo subito. */
            (void)consuma_riprova(c);
            imposta(c, PH_COL_NON_ABBINATO,
                    ph_t("questo PC non e' abbinato al telefono: serve l'abbinamento con il codice"), false,
                    NULL);
            continue;
        }
        if (fermati(c)) break;

        pthread_mutex_lock(&c->m);
        stato = c->ultimo.stato;
        pthread_mutex_unlock(&c->m);
        if (stato == PH_COL_PERSO) {
            /* il motivo resta quello della caduta */
            attendi(c, backoff);
            backoff = backoff > tetto / 2 ? tetto : backoff * 2;
        } else {
            imposta(c, PH_COL_CERCO, c->motivo_t, false, NULL);
            attendi(c, c->o.pausa_cerca_ms);
        }
    }

    /* Chiusura: la sessione e' gia' stata rilasciata dal ramo sopra; qui per
     * sicurezza si ripete (se non c'e' nulla non fa niente). */
    bool orfana = rilascia(c);
    if (orfana) {
        pthread_mutex_lock(&c->m);
        c->orfana_in_chiusura = true;
        pthread_mutex_unlock(&c->m);
    }
    imposta(c, PH_COL_CHIUSO, ph_t("chiusura richiesta"), true, NULL);
    return NULL;
}

/* ---- API --------------------------------------------------------------------------------*/
void ph_col_opzioni_predefinite(PhColOpzioni *o) { memset(o, 0, sizeof *o); }

PhCollegamento *ph_col_avvia(const PhColOpzioni *o, PhErrore *e) {
    if (!o || !o->jar_locale || !o->jar_locale[0]) {
        ph_errore(e, "collegamento: manca il percorso del jar del componente");
        return NULL;
    }
    riprova_residui();
    PhCollegamento *c = calloc(1, sizeof *c);
    if (!c) {
        ph_errore(e, "collegamento: memoria finita");
        return NULL;
    }
    c->o = *o;
    c->o.durata_scoperta_ms = predefinito(o->durata_scoperta_ms, 1500);
    c->o.timeout_collega_ms = predefinito(o->timeout_collega_ms, 8000);
    c->o.timeout_componente_ms = predefinito(o->timeout_componente_ms, 15000);
    c->o.timeout_abbinamento_ms = predefinito(o->timeout_abbinamento_ms, 15000);
    c->o.pausa_cerca_ms = predefinito(o->pausa_cerca_ms, 1000);
    c->o.riprova_iniziale_ms = predefinito(o->riprova_iniziale_ms, 1000);
    c->o.riprova_massimo_ms = predefinito(o->riprova_massimo_ms, 30000);
    if (c->o.riprova_massimo_ms < c->o.riprova_iniziale_ms)
        c->o.riprova_massimo_ms = c->o.riprova_iniziale_ms;
    c->o.riprova_dopo_blocco_ms = predefinito(o->riprova_dopo_blocco_ms, 3000);
    c->o.controllo_ms = predefinito(o->controllo_ms, 500);
    c->o.attesa_prestiti_ms = predefinito(o->attesa_prestiti_ms, 3000);
    c->jar = strdup(o->jar_locale);
    c->percorso_chiave = o->percorso_chiave ? strdup(o->percorso_chiave) : NULL;
    if (!c->jar || (o->percorso_chiave && !c->percorso_chiave)) {
        free(c->jar);
        free(c->percorso_chiave);
        free(c);
        ph_errore(e, "collegamento: memoria finita");
        return NULL;
    }
    c->o.jar_locale = c->jar;
    c->o.percorso_chiave = c->percorso_chiave;
    pthread_condattr_t at;
    pthread_condattr_init(&at);
    pthread_condattr_setclock(&at, CLOCK_MONOTONIC);
    pthread_mutex_init(&c->m, NULL);
    pthread_cond_init(&c->cv, &at);
    pthread_condattr_destroy(&at);
    c->ultimo.stato = PH_COL_CERCO;
    copia(c->ultimo.motivo, sizeof c->ultimo.motivo, ph_t("avvio"));
    if (pthread_create(&c->thread, NULL, thread_principale, c) != 0) {
        ph_errore(e, "collegamento: impossibile avviare il thread");
        pthread_cond_destroy(&c->cv);
        pthread_mutex_destroy(&c->m);
        free(c->jar);
        free(c->percorso_chiave);
        free(c);
        return NULL;
    }
    return c;
}

int ph_col_chiudi(PhCollegamento *c) {
    pthread_mutex_lock(&c->m);
    bool dentro = c->tid_valido && pthread_equal(c->tid, pthread_self());
    if (!dentro) c->stop = true;
    pthread_cond_broadcast(&c->cv);
    pthread_mutex_unlock(&c->m);
    if (dentro) {
        fputs("collegamento: ph_col_chiudi chiamata da un avviso, ignorata\n", stderr);
        return -1;
    }
    pthread_join(c->thread, NULL);
    int r = c->orfana_in_chiusura ? 1 : 0;
    riprova_residui();
    if (c->chiave) ph_chiave_libera(c->chiave);
    pthread_cond_destroy(&c->cv);
    pthread_mutex_destroy(&c->m);
    free(c->jar);
    free(c->percorso_chiave);
    free(c);
    return r;
}

void ph_col_stato(PhCollegamento *c, PhColAvviso *uscita) {
    pthread_mutex_lock(&c->m);
    *uscita = c->ultimo;
    pthread_mutex_unlock(&c->m);
    /* L'esito vale solo nell'avviso che conclude la richiesta: la fotografia non lo ripete. */
    uscita->esito_abbinamento = PH_COL_ABB_NESSUNO;
}

int ph_col_riconnetti_ora(PhCollegamento *c, PhErrore *e) {
    pthread_mutex_lock(&c->m);
    PhColStato st = c->ultimo.stato;
    /* Con un abbinamento accettato o in corso non si accetta: riprendere e abbinare
     * insieme farebbe pubblicare un CERCO preso per l'esito dell'abbinamento. */
    bool ok = st == PH_COL_PERSO || st == PH_COL_CERCO ||
              (st == PH_COL_NON_ABBINATO && !c->abbina_richiesta && !c->abbina_in_corso);
    if (ok) {
        c->riprova_ora = true;
        pthread_cond_broadcast(&c->cv);
    }
    pthread_mutex_unlock(&c->m);
    if (!ok) return ph_errore(e, "riconnessione: non c'e' nulla da riprovare in questo stato");
    return 0;
}

static int accoda_abbina(PhCollegamento *c, const char *host, uint16_t porta, const char *codice,
                         bool esplicito, PhErrore *e) {
    if (!codice || !codice[0] || strlen(codice) >= LUN_CODICE ||
        (esplicito && (!host || !host[0] || strlen(host) >= LUN_HOST || porta == 0)))
        return ph_errore(e, "abbinamento: codice, indirizzo o porta non validi");
    pthread_mutex_lock(&c->m);
    bool ok = c->ultimo.stato == PH_COL_NON_ABBINATO && !c->abbina_richiesta && !c->abbina_in_corso &&
              !c->riprova_ora && !c->riprendendo;
    if (ok) {
        c->abbina_esplicito = esplicito;
        if (esplicito) {
            copia(c->abbina_host, sizeof c->abbina_host, host);
            c->abbina_porta = porta;
        }
        copia(c->abbina_codice, sizeof c->abbina_codice, codice);
        c->abbina_richiesta = true;
        pthread_cond_broadcast(&c->cv);
    }
    pthread_mutex_unlock(&c->m);
    if (!ok) return ph_errore(e, "abbinamento: non si puo' chiedere ora (serve lo stato non abbinato)");
    return 0;
}

int ph_col_abbina(PhCollegamento *c, const char *codice, PhErrore *e) {
    return accoda_abbina(c, NULL, 0, codice, false, e);
}

int ph_col_abbina_a(PhCollegamento *c, const char *host, uint16_t porta, const char *codice,
                    PhErrore *e) {
    return accoda_abbina(c, host, porta, codice, true, e);
}

PhColPrestito *ph_col_presta(PhCollegamento *c, PhErrore *e) {
    PhColPrestito *p = malloc(sizeof *p);
    if (!p) {
        ph_errore(e, "prestito: memoria finita");
        return NULL;
    }
    pthread_mutex_lock(&c->m);
    Sessione *s = c->sess;
    bool buono = s && (c->ultimo.stato == PH_COL_COLLEGATO || c->ultimo.stato == PH_COL_BLOCCATO);
    if (buono) {
        pthread_mutex_lock(&s->m);
        if (s->perso)
            buono = false;
        else
            s->prestiti++;
        pthread_mutex_unlock(&s->m);
    }
    pthread_mutex_unlock(&c->m);
    if (!buono) {
        free(p);
        ph_errore(e, "prestito: nessun telefono collegato");
        return NULL;
    }
    p->s = s;
    return p;
}

PhAdb *ph_col_prestito_adb(const PhColPrestito *p) { return p->s->adb; }
PhComponente *ph_col_prestito_componente(const PhColPrestito *p) { return p->s->comp; }
PhDati *ph_col_prestito_dati(const PhColPrestito *p) { return p->s->dati; }
const PhColTelefono *ph_col_prestito_telefono(const PhColPrestito *p) { return &p->s->tel; }

bool ph_col_prestito_attivo(const PhColPrestito *p) {
    Sessione *s = p->s;
    pthread_mutex_lock(&s->m);
    bool r = !s->perso;
    pthread_mutex_unlock(&s->m);
    return r;
}

void ph_col_restituisci(PhColPrestito *p) {
    Sessione *s = p->s;
    free(p);
    pthread_mutex_lock(&s->m);
    s->prestiti--;
    bool distruggi = s->prestiti == 0 && s->orfana;
    if (s->prestiti == 0) pthread_cond_broadcast(&s->cv);
    pthread_mutex_unlock(&s->m);
    if (distruggi) (void)sessione_distruggi(s); /* puo' durare (chiude componente e connessione) */
}
