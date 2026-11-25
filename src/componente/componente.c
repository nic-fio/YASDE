/* Avvio del servizio sul telefono, canale comandi (richieste/risposte/eventi)
 * e invio dell'input. Protocollo: docs/protocollo.md. */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "componente/interno.h"
#include "file/file.h"

/* Nome del jar sul telefono: cambia a ogni avvio (8 cifre esadecimali casuali),
 * cosi' il guardiano di un servizio appena morto non cancella il jar del
 * servizio successivo. La casualita' viene dal sistema, non da rand(). */
static int genera_nome_jar(char *uscita, size_t n, PhErrore *e)
{
    uint32_t r = 0;
    size_t letti = 0;
    FILE *f = fopen("/dev/urandom", "rb");
    if (f != NULL) {
        letti = fread(&r, 1, sizeof r, f);
        fclose(f);
    }
    if (letti != sizeof r)
        return ph_errore(e, "impossibile ottenere un nome casuale per il servizio");
    int k = snprintf(uscita, n, PH_JAR_PREFISSO "%08x.jar", (unsigned)r);
    if (k < 0 || (size_t)k >= n)
        return ph_errore(e, "nome del servizio troppo lungo");
    return 0;
}

/* ---- Vita e morte --------------------------------------------------------*/
static void segna_morto(PhComponente *c)
{
    pthread_mutex_lock(&c->m);
    atomic_store(&c->morto, true);
    pthread_cond_broadcast(&c->cv);
    pthread_mutex_unlock(&c->m);
}

void ph_componente_riferimento(PhComponente *c)
{
    atomic_fetch_add(&c->rif, 1);
}

void ph_componente_rilascia(PhComponente *c)
{
    if (atomic_fetch_sub(&c->rif, 1) != 1)
        return;
    pthread_mutex_destroy(&c->m);
    pthread_cond_destroy(&c->cv);
    pthread_mutex_destroy(&c->m_in);
    pthread_mutex_destroy(&c->m_asc);
    ph_buffer_libera(&c->risp_corpo);
    ph_buffer_libera(&c->buf_in);
    free(c);
}

void ph_componente_prova_timeout(PhComponente *c, int timeout_ms)
{
    c->timeout_ms = timeout_ms;
}

int64_t (*ph_boot_hook)(void) = NULL;
int ph_presenza_fetta_max_ms = 30000;

int64_t ph_boot_ms(void)
{
    if (ph_boot_hook != NULL)
        return ph_boot_hook();
    struct timespec t;
    clock_gettime(CLOCK_BOOTTIME, &t);
    return (int64_t)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

int ph_presenza_resta_ms(int64_t ultima, int intervallo_ms, int64_t ora)
{
    int64_t r = ultima + intervallo_ms - ora;
    if (r <= 0)
        return 0;
    return r > 0x7fffffff ? 0x7fffffff : (int)r;
}

int ph_presenza_fetta_ms(int resta)
{
    return resta > ph_presenza_fetta_max_ms ? ph_presenza_fetta_max_ms : resta;
}

void ph_componente_prova_presenza(PhComponente *c, int intervallo_ms)
{
    pthread_mutex_lock(&c->m);
    c->presenza_ms = intervallo_ms;
    pthread_cond_broadcast(&c->cv);
    pthread_mutex_unlock(&c->m);
}

bool ph_componente_vivo(PhComponente *c)
{
    return c != NULL && !atomic_load(&c->morto);
}

/* ---- Thread interni ------------------------------------------------------*/
static void consegna_risposta(PhComponente *c, uint8_t tipo, const uint8_t *corpo, size_t n)
{
    pthread_mutex_lock(&c->m);
    /* Una risposta senza richiesta in attesa (arrivata dopo un timeout, o
     * inventata) si scarta: non deve finire alla richiesta successiva. */
    if (c->in_attesa && !c->pronta) {
        ph_buffer_svuota(&c->risp_corpo);
        c->risp_tipo = ph_buffer_aggiungi(&c->risp_corpo, corpo, n) == 0 ? tipo : 0;
        c->pronta = true;
        pthread_cond_broadcast(&c->cv);
    }
    pthread_mutex_unlock(&c->m);
}

static void consegna_evento(PhComponente *c, uint8_t tipo, const uint8_t *corpo, size_t n)
{
    if (atomic_load(&c->chiuso))
        return;
    if (tipo == PH_MSG_VIDEO_EVENTO) {
        if (c->evento == NULL)
            return;
        PhLettore l = {corpo, n, 0, false};
        uint32_t id = ph_leggi_u32(&l);
        uint8_t che = ph_leggi_u8(&l);
        uint16_t a = ph_leggi_u16(&l);
        uint16_t b = ph_leggi_u16(&l);
        if (!l.rotto)
            c->evento(id, che, a, b, c->utente);
        return;
    }
    /* Il mutex resta preso durante la chiamata: chi toglie l'ascoltatore
     * aspetta che finisca, e dopo non verra' piu' chiamato. Eventi di tipo
     * non registrato: ignorati. */
    pthread_mutex_lock(&c->m_asc);
    for (size_t i = 0; i < 8; i++) {
        if (c->asc[i].usato && tipo >= c->asc[i].da && tipo <= c->asc[i].a) {
            c->asc[i].f(tipo, corpo, n, c->asc[i].utente);
            break;
        }
    }
    pthread_mutex_unlock(&c->m_asc);
}

/* Legge i messaggi del canale comandi e separa risposte ed eventi. Esce
 * quando il canale cade, un messaggio e' impossibile, o arriva `fermo`. */
static void *thread_comandi(void *arg)
{
    PhComponente *c = arg;
    uint8_t testa[5];
    size_t fatti_testa = 0, fatti_corpo = 0;
    uint32_t lunghezza = 0;
    int fase = 0;
    PhBuffer corpo = PH_BUFFER_VUOTO;

    while (!atomic_load(&c->fermo)) {
        if (fase == 0) {
            int r = ph_rip_leggi(c->comandi, testa, &fatti_testa, sizeof testa, PH_FETTA_MS, NULL);
            if (r == PH_CANALE_TEMPO)
                continue;
            if (r != 1)
                break;
            lunghezza = ((uint32_t)testa[1] << 24) | ((uint32_t)testa[2] << 16) |
                        ((uint32_t)testa[3] << 8) | testa[4];
            /* Lunghezza falsa: oltre il massimo del protocollo non si alloca nulla. */
            if (lunghezza > PH_MSG_MAX_CORPO)
                break;
            if (ph_buffer_riserva(&corpo, lunghezza) != 0)
                break;
            fatti_corpo = 0;
            fase = 1;
        }
        if (fase == 1) {
            int r = 1;
            if (lunghezza > 0)
                r = ph_rip_leggi(c->comandi, corpo.dati, &fatti_corpo, lunghezza, PH_FETTA_MS, NULL);
            if (r == PH_CANALE_TEMPO)
                continue;
            if (r != 1)
                break;
            if (testa[0] == PH_MSG_OK || testa[0] == PH_MSG_ERRORE)
                consegna_risposta(c, testa[0], corpo.dati, lunghezza);
            else
                consegna_evento(c, testa[0], corpo.dati, lunghezza);
            fase = 0;
            fatti_testa = 0;
        }
    }
    ph_buffer_libera(&corpo);
    segna_morto(c);
    return NULL;
}

/* Dopo PRONTO il canale shell porta solo righe di log: vanno lette (altrimenti
 * il controllo di flusso si riempie) e buttate. Se si chiude, il servizio e'
 * terminato. */
static void *thread_shell(void *arg)
{
    PhComponente *c = arg;
    uint8_t scarto[512];
    while (!atomic_load(&c->fermo)) {
        ssize_t r = ph_canale_leggi(c->shell, scarto, sizeof scarto, PH_FETTA_MS, NULL);
        if (r == PH_CANALE_TEMPO)
            continue;
        if (r <= 0)
            break;
    }
    segna_morto(c);
    return NULL;
}

/* Manda PRESENZA quando da presenza_ms non parte nessuna richiesta. Passa dalla
 * strada delle richieste normali (il turno): al peggio aspetta una richiesta in
 * corso, che dura al massimo il tempo di risposta; la finestra del telefono
 * (30 minuti) e' sei volte l'intervallo, quindi un ritardo di quel genere non
 * la fa saltare. Nessun lock durante la rete: la scadenza si aspetta su cv. */
static void *thread_presenza(void *arg)
{
    PhComponente *c = arg;
    PhBuffer msg = PH_BUFFER_VUOTO;
    if (ph_msg_inizia(&msg, PH_MSG_PRESENZA) != 0 || ph_msg_finisci(&msg) != 0) {
        ph_buffer_libera(&msg);
        return NULL; /* senza memoria: niente presenza, il resto funziona */
    }
    pthread_mutex_lock(&c->m);
    while (!atomic_load(&c->fermo) && !atomic_load(&c->morto) && !atomic_load(&c->chiudendo)) {
        int resta = ph_presenza_resta_ms(atomic_load(&c->ultima_ms), c->presenza_ms, ph_boot_ms());
        if (resta > 0) {
            /* L'attesa e' a fette (orologio monotono): dopo una sospensione del PC
             * la condizione si ricalcola con l'orologio che include il sonno. */
            resta = ph_presenza_fetta_ms(resta);
            struct timespec t;
            clock_gettime(CLOCK_MONOTONIC, &t);
            t.tv_sec += resta / 1000;
            t.tv_nsec += (long)(resta % 1000) * 1000000L;
            if (t.tv_nsec >= 1000000000L) {
                t.tv_sec++;
                t.tv_nsec -= 1000000000L;
            }
            pthread_cond_timedwait(&c->cv, &c->m, &t);
            continue;
        }
        pthread_mutex_unlock(&c->m);
        PhErrore e;
        /* Fallimento: una riga senza dati. Un telefono che non risponde entro il
         * tempo massimo vale come per ogni richiesta (componente non piu' vivo:
         * una risposta tardiva sfalserebbe le richieste dopo); un ERRORE no. */
        if (ph_componente_richiedi(c, &msg, NULL, c->timeout_ms, &e) != 0 && !atomic_load(&c->fermo))
            fputs("componente: PRESENZA senza risposta valida dal telefono\n", stderr);
        pthread_mutex_lock(&c->m);
    }
    pthread_mutex_unlock(&c->m);
    ph_buffer_libera(&msg);
    return NULL;
}

/* ---- Richieste -----------------------------------------------------------*/
int ph_componente_richiedi(PhComponente *c, const PhBuffer *messaggio, PhBuffer *corpo_ok,
                           int timeout_ms, PhErrore *e)
{
    return ph_componente_richiedi_coda(c, messaggio, corpo_ok, timeout_ms, -1, e);
}

int ph_componente_richiedi_coda(PhComponente *c, const PhBuffer *messaggio, PhBuffer *corpo_ok,
                                int timeout_ms, int coda_ms, PhErrore *e)
{
    const uint8_t tipo_richiesta = messaggio->dati[0];
    if (timeout_ms <= 0)
        timeout_ms = PH_TIMEOUT_RICHIESTA_MS;

    /* Turno: una richiesta alla volta. Si aspetta a tempo (coda_ms < 0 = senza
     * limite) e la morte del componente sveglia chi e' in coda. */
    const int64_t fine_coda = coda_ms < 0 ? -1 : ph_ora_ms() + coda_ms;
    pthread_mutex_lock(&c->m);
    for (;;) {
        if (atomic_load(&c->morto) || (tipo_richiesta == PH_MSG_PRESENZA && atomic_load(&c->chiudendo))) {
            pthread_mutex_unlock(&c->m);
            return ph_errore(e, "il servizio sul telefono non e' piu' raggiungibile");
        }
        if (!c->rich_occupata)
            break;
        if (fine_coda < 0) {
            pthread_cond_wait(&c->cv, &c->m);
        } else {
            int resta = ph_resta_ms(fine_coda);
            if (resta == 0) {
                pthread_mutex_unlock(&c->m);
                return ph_errore(e, "richiesta 0x%02x: il canale comandi e' occupato da un'altra richiesta",
                                 tipo_richiesta);
            }
            struct timespec t;
            clock_gettime(CLOCK_MONOTONIC, &t);
            t.tv_sec += resta / 1000;
            t.tv_nsec += (long)(resta % 1000) * 1000000L;
            if (t.tv_nsec >= 1000000000L) {
                t.tv_sec++;
                t.tv_nsec -= 1000000000L;
            }
            pthread_cond_timedwait(&c->cv, &c->m, &t);
        }
    }
    c->rich_occupata = true;
    c->in_attesa = true;
    c->pronta = false;
    pthread_mutex_unlock(&c->m);

    int esito = -1;
    /* Il conto della presenza riparte quando la richiesta parte davvero (turno preso). */
    atomic_store(&c->ultima_ms, ph_boot_ms());
    if (ph_canale_scrivi(c->comandi, messaggio->dati, messaggio->lunghezza, e) != 0) {
        ph_errore_contesto(e, "invio della richiesta al telefono");
        pthread_mutex_lock(&c->m);
        c->in_attesa = false;
        atomic_store(&c->morto, true);
        c->rich_occupata = false;
        pthread_cond_broadcast(&c->cv);
        pthread_mutex_unlock(&c->m);
        return -1;
    }

    const int64_t scadenza = ph_ora_ms() + timeout_ms;
    pthread_mutex_lock(&c->m);
    while (!c->pronta && !atomic_load(&c->morto)) {
        int resta = ph_resta_ms(scadenza);
        if (resta == 0)
            break;
        struct timespec t;
        clock_gettime(CLOCK_MONOTONIC, &t);
        t.tv_sec += resta / 1000;
        t.tv_nsec += (long)(resta % 1000) * 1000000L;
        if (t.tv_nsec >= 1000000000L) {
            t.tv_sec++;
            t.tv_nsec -= 1000000000L;
        }
        pthread_cond_timedwait(&c->cv, &c->m, &t);
    }
    if (c->pronta) {
        c->in_attesa = false;
        if (c->risp_tipo == PH_MSG_OK) {
            if (corpo_ok != NULL) {
                ph_buffer_svuota(corpo_ok);
                if (ph_buffer_aggiungi(corpo_ok, c->risp_corpo.dati, c->risp_corpo.lunghezza) != 0)
                    ph_errore(e, "memoria esaurita leggendo la risposta del telefono");
                else
                    esito = 0;
            } else {
                esito = 0;
            }
        } else if (c->risp_tipo == PH_MSG_ERRORE) {
            PhLettore l = {c->risp_corpo.dati, c->risp_corpo.lunghezza, 0, false};
            char frase[200];
            ph_leggi_stringa(&l, frase, sizeof frase);
            if (l.rotto || frase[0] == '\0')
                ph_errore(e, "il telefono ha rifiutato la richiesta 0x%02x", tipo_richiesta);
            else if (strcmp(frase, "tipo sconosciuto") == 0)
                ph_errore(e, "il servizio sul telefono non conosce la richiesta 0x%02x", tipo_richiesta);
            else
                ph_errore(e, "%s", frase);
        } else {
            ph_errore(e, "memoria esaurita leggendo la risposta del telefono");
        }
    } else {
        c->in_attesa = false;
        if (atomic_load(&c->morto)) {
            ph_errore(e, "il servizio sul telefono e' caduto durante la richiesta 0x%02x",
                      tipo_richiesta);
        } else {
            /* Senza risposta il canale non e' piu' allineato (una risposta tardiva
             * finirebbe alla richiesta dopo): il componente si considera morto. */
            atomic_store(&c->morto, true);
            pthread_cond_broadcast(&c->cv);
            ph_errore(e, "il telefono non ha risposto alla richiesta 0x%02x entro %d ms",
                      tipo_richiesta, timeout_ms);
        }
    }
    c->rich_occupata = false;
    pthread_cond_broadcast(&c->cv);
    pthread_mutex_unlock(&c->m);
    return esito;
}

/* ---- Avvio e chiusura ----------------------------------------------------*/
static void smonta(PhComponente *c)
{
    /* Prima di tutto morto+fermo: chi sta chiamando richieste o input esce
     * subito invece di scrivere su canali che stiamo per chiudere. */
    pthread_mutex_lock(&c->m);
    atomic_store(&c->morto, true);
    atomic_store(&c->fermo, true);
    pthread_cond_broadcast(&c->cv);
    pthread_mutex_unlock(&c->m);
    if (c->th_comandi_ok)
        pthread_join(c->th_comandi, NULL);
    if (c->th_shell_ok)
        pthread_join(c->th_shell, NULL);
    if (c->th_presenza_ok)
        pthread_join(c->th_presenza, NULL);
    c->th_comandi_ok = c->th_shell_ok = c->th_presenza_ok = false;
    /* I canali si chiudono tenendo i mutex di chi li usa: una richiesta o un
     * messaggio di input gia' in corso finisce prima, gli altri vedono morto. */
    /* morto e' alzato: nessuna richiesta nuova prende il turno; si aspetta
     * quella che l'ha gia' preso (svegliata dal broadcast, esce subito). */
    pthread_mutex_lock(&c->m);
    while (c->rich_occupata)
        pthread_cond_wait(&c->cv, &c->m);
    pthread_mutex_unlock(&c->m);
    if (c->comandi != NULL)
        ph_canale_chiudi(c->comandi);
    c->comandi = NULL;
    pthread_mutex_lock(&c->m_in);
    if (c->input != NULL)
        ph_canale_chiudi(c->input);
    c->input = NULL;
    pthread_mutex_unlock(&c->m_in);
    /* Il canale shell per ultimo: chiuderlo e' la rete di sicurezza che ferma il servizio. */
    if (c->shell != NULL)
        ph_canale_chiudi(c->shell);
    c->shell = NULL;
}

static void rimuovi_ritorno_a_capo(char *s)
{
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == '\r' || s[n - 1] == '\n'))
        s[--n] = '\0';
}

/* Legge dal canale shell finche' compare la riga PRONTO. Le righe di log
 * prima di PRONTO si buttano ma l'ultima si ricorda per il messaggio d'errore. */
static int aspetta_pronto(PhCanale *shell, int timeout_ms, PhErrore *e)
{
    const int64_t scadenza = ph_ora_ms() + timeout_ms;
    PhBuffer acc = PH_BUFFER_VUOTO;
    char ultima[160] = "";
    int esito = -1;

    for (;;) {
        /* Righe complete gia' in acc. */
        for (;;) {
            uint8_t *fine = acc.dati ? memchr(acc.dati, '\n', acc.lunghezza) : NULL;
            if (fine == NULL)
                break;
            size_t n = (size_t)(fine - acc.dati);
            char riga[160];
            size_t copia = n < sizeof riga - 1 ? n : sizeof riga - 1;
            memcpy(riga, acc.dati, copia);
            riga[copia] = '\0';
            rimuovi_ritorno_a_capo(riga);
            size_t resto = acc.lunghezza - n - 1;
            memmove(acc.dati, fine + 1, resto);
            acc.lunghezza = resto;
            acc.dati[resto] = '\0';
            if (strcmp(riga, "PRONTO") == 0) {
                esito = 0;
                goto fatto;
            }
            if (riga[0] != '\0')
                snprintf(ultima, sizeof ultima, "%s", riga);
        }
        /* Una riga senza fine non deve crescere senza limite. */
        if (acc.lunghezza > 4096)
            ph_buffer_svuota(&acc);

        int resta = ph_resta_ms(scadenza);
        if (resta == 0) {
            ph_errore(e, "il servizio sul telefono non ha detto PRONTO entro %d ms%s%s",
                      timeout_ms, ultima[0] ? "; ultima riga: " : "", ultima);
            goto fatto;
        }
        uint8_t blocco[512];
        ssize_t r = ph_canale_leggi(shell, blocco, sizeof blocco, resta, e);
        if (r > 0) {
            if (ph_buffer_aggiungi(&acc, blocco, (size_t)r) != 0) {
                ph_errore(e, "memoria esaurita aspettando il servizio");
                goto fatto;
            }
        } else if (r == PH_CANALE_TEMPO) {
            continue; /* il controllo della scadenza sta in cima */
        } else if (r == PH_CANALE_CHIUSO) {
            if (acc.lunghezza > 0 && ultima[0] == '\0') {
                size_t n = acc.lunghezza < sizeof ultima - 1 ? acc.lunghezza : sizeof ultima - 1;
                memcpy(ultima, acc.dati, n);
                ultima[n] = '\0';
                rimuovi_ritorno_a_capo(ultima);
            }
            ph_errore(e, "il servizio sul telefono e' terminato prima di PRONTO%s%s",
                      ultima[0] ? "; ultima riga: " : "", ultima);
            goto fatto;
        } else {
            ph_errore_contesto(e, "attesa di PRONTO dal servizio");
            goto fatto;
        }
    }
fatto:
    ph_buffer_libera(&acc);
    return esito;
}

/* Apre un canale del protocollo e scrive la riga che ne dice il tipo. */
static PhCanale *apri_tipo(PhAdb *a, const char *riga, int timeout_ms, PhErrore *e)
{
    PhCanale *k = ph_canale_apri(a, "localabstract:yasde", timeout_ms, e);
    if (k == NULL) {
        ph_errore_contesto(e, "apertura del canale '%s'", riga);
        return NULL;
    }
    char testo[96];
    int n = snprintf(testo, sizeof testo, "%s\n", riga);
    if (n < 0 || (size_t)n >= sizeof testo || ph_canale_scrivi(k, testo, (size_t)n, e) != 0) {
        ph_errore_contesto(e, "apertura del canale '%s'", riga);
        ph_canale_chiudi(k);
        return NULL;
    }
    return k;
}

static int saluta(PhComponente *c, PhTelefonoInfo *info, int timeout_ms, PhErrore *e)
{
    PhBuffer msg = PH_BUFFER_VUOTO, risp = PH_BUFFER_VUOTO;
    int esito = -1;
    if (ph_msg_inizia(&msg, PH_MSG_SALUTO) != 0 || ph_msg_u16(&msg, PH_PROTOCOLLO_VERSIONE) != 0 ||
        ph_msg_finisci(&msg) != 0) {
        ph_errore(e, "memoria esaurita preparando il saluto");
        goto fatto;
    }
    if (ph_componente_richiedi(c, &msg, &risp, timeout_ms, e) != 0) {
        ph_errore_contesto(e, "saluto al telefono");
        goto fatto;
    }
    PhLettore l = {risp.dati, risp.lunghezza, 0, false};
    PhTelefonoInfo t;
    memset(&t, 0, sizeof t);
    uint16_t versione = ph_leggi_u16(&l);
    t.api = ph_leggi_u16(&l);
    t.larghezza = ph_leggi_u16(&l);
    t.altezza = ph_leggi_u16(&l);
    t.densita = ph_leggi_u16(&l);
    ph_leggi_stringa(&l, t.modello, sizeof t.modello);
    if (l.rotto) {
        ph_errore(e, "la risposta al saluto del telefono e' troncata");
        goto fatto;
    }
    if (versione != PH_PROTOCOLLO_VERSIONE) {
        ph_errore(e, "il servizio sul telefono parla la versione %u del protocollo, il PC la %u",
                  (unsigned)versione, (unsigned)PH_PROTOCOLLO_VERSIONE);
        goto fatto;
    }
    if (info != NULL)
        *info = t;
    esito = 0;
fatto:
    ph_buffer_libera(&msg);
    ph_buffer_libera(&risp);
    return esito;
}

PhComponente *ph_componente_avvia(PhAdb *a, const char *jar_locale, PhEvento evento,
                                  void *utente, PhTelefonoInfo *info, int timeout_ms,
                                  PhErrore *e)
{
    if (a == NULL || jar_locale == NULL) {
        ph_errore(e, "avvio del servizio: connessione o file del servizio mancanti");
        return NULL;
    }
    if (timeout_ms <= 0)
        timeout_ms = PH_TIMEOUT_PREDEFINITO_MS;

    bool jar_copiato = false;
    PhComponente *c = calloc(1, sizeof *c);
    if (c == NULL) {
        ph_errore(e, "memoria esaurita avviando il servizio");
        return NULL;
    }
    c->adb = a;
    c->evento = evento;
    c->utente = utente;
    c->timeout_ms = PH_TIMEOUT_RICHIESTA_MS; /* il timeout d'avvio vale solo per avvio e saluto */
    atomic_init(&c->rif, 1);
    atomic_init(&c->morto, false);
    atomic_init(&c->chiuso, false);
    atomic_init(&c->fermo, false);
    atomic_init(&c->prossimo_id, 1);
    atomic_init(&c->ultima_ms, ph_boot_ms());
    atomic_init(&c->chiudendo, false);
    c->presenza_ms = PH_PRESENZA_INTERVALLO_S * 1000;
    pthread_condattr_t ca;
    pthread_condattr_init(&ca);
    pthread_condattr_setclock(&ca, CLOCK_MONOTONIC);
    pthread_mutex_init(&c->m, NULL);
    pthread_cond_init(&c->cv, &ca);
    pthread_condattr_destroy(&ca);
    pthread_mutex_init(&c->m_in, NULL);
    pthread_mutexattr_t ma;
    pthread_mutexattr_init(&ma);
    pthread_mutexattr_settype(&ma, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&c->m_asc, &ma);
    pthread_mutexattr_destroy(&ma);

    if (e != NULL)
        e->testo[0] = '\0';
    if (genera_nome_jar(c->jar_remoto, sizeof c->jar_remoto, e) != 0)
        goto errore;
    /* Vero PRIMA della copia: se si interrompe a meta' un jar parziale resta sul
     * telefono, e ogni uscita d'errore da qui in poi tenta di toglierlo. */
    jar_copiato = true;
    if (ph_file_invia(a, jar_locale, c->jar_remoto, NULL, NULL, NULL, e) != PH_FILE_RIUSCITO) {
        ph_errore_contesto(e, "copia del servizio sul telefono");
        goto errore;
    }

    char comando[PH_JAR_REMOTO_MAX + 80];
    snprintf(comando, sizeof comando, "shell:CLASSPATH=%s app_process / it.yasde.Servizio",
             c->jar_remoto);
    c->shell = ph_canale_apri(a, comando, timeout_ms, e);
    if (c->shell == NULL) {
        ph_errore_contesto(e, "lancio del servizio sul telefono");
        goto errore;
    }
    if (aspetta_pronto(c->shell, timeout_ms, e) != 0)
        goto errore;

    c->comandi = apri_tipo(a, "comandi", timeout_ms, e);
    if (c->comandi == NULL)
        goto errore;
    c->input = apri_tipo(a, "input", timeout_ms, e);
    if (c->input == NULL)
        goto errore;

    if (pthread_create(&c->th_comandi, NULL, thread_comandi, c) != 0) {
        ph_errore(e, "impossibile avviare il thread del canale comandi");
        goto errore;
    }
    c->th_comandi_ok = true;
    if (pthread_create(&c->th_shell, NULL, thread_shell, c) != 0) {
        ph_errore(e, "impossibile avviare il thread del canale shell");
        goto errore;
    }
    c->th_shell_ok = true;

    if (saluta(c, info, timeout_ms, e) != 0)
        goto errore;
    if (pthread_create(&c->th_presenza, NULL, thread_presenza, c) != 0) {
        ph_errore(e, "impossibile avviare il thread della presenza");
        goto errore;
    }
    c->th_presenza_ok = true;
    return c;

errore:
    smonta(c);
    if (jar_copiato) {
        /* Il servizio non e' partito bene: il jar non deve restare sul telefono.
         * Esito ignorato, e' solo pulizia. */
        PhErrore ignorato;
        char rm[PH_JAR_REMOTO_MAX + 16];
        snprintf(rm, sizeof rm, "rm -f %s", c->jar_remoto);
        (void)ph_shell_esegui(a, rm, NULL, NULL, NULL, 3000, &ignorato);
    }
    ph_componente_rilascia(c);
    return NULL;
}

void ph_componente_chiudi(PhComponente *c)
{
    if (c == NULL)
        return;
    /* Prima di CHIUDI: da qui nessuna PRESENZA nuova (il thread esce, richiedi la rifiuta). */
    pthread_mutex_lock(&c->m);
    atomic_store(&c->chiudendo, true);
    pthread_cond_broadcast(&c->cv);
    pthread_mutex_unlock(&c->m);
    if (!atomic_load(&c->morto)) {
        PhBuffer msg = PH_BUFFER_VUOTO;
        /* Il servizio risponde OK prima di terminare: attesa breve (anche del turno,
         * se una richiesta e' ferma), poi si chiude comunque: smonta sveglia chi aspetta. */
        if (ph_msg_inizia(&msg, PH_MSG_CHIUDI) == 0 && ph_msg_finisci(&msg) == 0)
            (void)ph_componente_richiedi_coda(c, &msg, NULL, 2000, 2000, NULL);
        ph_buffer_libera(&msg);
    }
    atomic_store(&c->chiuso, true);
    smonta(c);
    /* I flussi video ancora aperti tengono il loro riferimento: la struttura
     * resta valida finche' non li chiudono. */
    ph_componente_rilascia(c);
}

/* ---- Input ---------------------------------------------------------------*/
/* Spedisce il messaggio gia' costruito in buf_in; m_in e' tenuto da chi chiama. */
static int spedisci_input(PhComponente *c, PhErrore *e)
{
    if (ph_msg_finisci(&c->buf_in) != 0)
        return ph_errore(e, "messaggio di input troppo lungo");
    if (atomic_load(&c->morto))
        return ph_errore(e, "il servizio sul telefono non e' piu' raggiungibile");
    if (ph_canale_scrivi(c->input, c->buf_in.dati, c->buf_in.lunghezza, e) != 0) {
        segna_morto(c);
        return ph_errore_contesto(e, "invio dell'input al telefono");
    }
    return 0;
}

#define INPUT_INIZIO(tipo)                                                               \
    if (c == NULL)                                                                       \
        return ph_errore(e, "componente mancante");                                      \
    pthread_mutex_lock(&c->m_in);                                                        \
    int ris = -1;                                                                        \
    if (ph_msg_inizia(&c->buf_in, (tipo)) != 0)                                          \
        goto memoria;

#define INPUT_FINE                                                                       \
    ris = spedisci_input(c, e);                                                          \
    pthread_mutex_unlock(&c->m_in);                                                      \
    return ris;                                                                          \
memoria:                                                                                 \
    pthread_mutex_unlock(&c->m_in);                                                      \
    return ph_errore(e, "messaggio di input non costruibile (memoria o testo troppo lungo)");

int ph_input_tocco(PhComponente *c, uint32_t id, uint16_t larghezza, uint16_t altezza,
                   uint8_t azione, uint16_t x, uint16_t y, uint8_t pulsanti, PhErrore *e)
{
    INPUT_INIZIO(PH_MSG_TOCCO)
    if (ph_msg_u32(&c->buf_in, id) || ph_msg_u16(&c->buf_in, larghezza) ||
        ph_msg_u16(&c->buf_in, altezza) || ph_msg_u8(&c->buf_in, azione) ||
        ph_msg_u16(&c->buf_in, x) || ph_msg_u16(&c->buf_in, y) || ph_msg_u8(&c->buf_in, pulsanti))
        goto memoria;
    INPUT_FINE
}

int ph_input_rotella(PhComponente *c, uint32_t id, uint16_t larghezza, uint16_t altezza,
                     uint16_t x, uint16_t y, int16_t orizzontale, int16_t verticale, PhErrore *e)
{
    INPUT_INIZIO(PH_MSG_ROTELLA)
    if (ph_msg_u32(&c->buf_in, id) || ph_msg_u16(&c->buf_in, larghezza) ||
        ph_msg_u16(&c->buf_in, altezza) || ph_msg_u16(&c->buf_in, x) || ph_msg_u16(&c->buf_in, y) ||
        ph_msg_i16(&c->buf_in, orizzontale) || ph_msg_i16(&c->buf_in, verticale))
        goto memoria;
    INPUT_FINE
}

int ph_input_tasto(PhComponente *c, uint32_t id, bool giu, uint32_t codice, uint32_t meta,
                   PhErrore *e)
{
    INPUT_INIZIO(PH_MSG_TASTO)
    if (ph_msg_u32(&c->buf_in, id) || ph_msg_u8(&c->buf_in, giu ? 0 : 1) ||
        ph_msg_u32(&c->buf_in, codice) || ph_msg_u32(&c->buf_in, meta))
        goto memoria;
    INPUT_FINE
}

int ph_input_pizzico(PhComponente *c, uint32_t id, uint16_t larghezza, uint16_t altezza, uint8_t fase,
                     uint16_t x, uint16_t y, int16_t passo, PhErrore *e)
{
    /* Argomenti controllati prima di prendere qualunque lock: se non validi
     * non si manda nulla. Il passo fuori misura invece si limita (contratto). */
    if (fase > PH_PIZZICO_ANNULLA)
        return ph_errore(e, "pizzico: fase %u sconosciuta", (unsigned)fase);
    if (larghezza == 0 || altezza == 0 || x >= larghezza || y >= altezza)
        return ph_errore(e, "pizzico: posizione %u,%u fuori dal video %ux%u", (unsigned)x, (unsigned)y,
                         (unsigned)larghezza, (unsigned)altezza);
    if (passo > 1200)
        passo = 1200;
    else if (passo < -1200)
        passo = -1200;
    INPUT_INIZIO(PH_MSG_PIZZICO)
    if (ph_msg_u32(&c->buf_in, id) || ph_msg_u16(&c->buf_in, larghezza) ||
        ph_msg_u16(&c->buf_in, altezza) || ph_msg_u8(&c->buf_in, fase) || ph_msg_u16(&c->buf_in, x) ||
        ph_msg_u16(&c->buf_in, y) || ph_msg_i16(&c->buf_in, passo))
        goto memoria;
    INPUT_FINE
}

int ph_input_testo(PhComponente *c, uint32_t id, const char *testo, PhErrore *e)
{
    if (testo == NULL)
        return ph_errore(e, "testo da inserire mancante");
    INPUT_INIZIO(PH_MSG_TESTO)
    if (ph_msg_u32(&c->buf_in, id) || ph_msg_stringa(&c->buf_in, testo))
        goto memoria;
    INPUT_FINE
}

int ph_input_indietro(PhComponente *c, uint32_t id, PhErrore *e)
{
    INPUT_INIZIO(PH_MSG_INDIETRO)
    if (ph_msg_u32(&c->buf_in, id))
        goto memoria;
    INPUT_FINE
}

/* ---- Per gli altri pezzi -------------------------------------------------*/
int ph_componente_richiesta(PhComponente *c, uint8_t tipo, const uint8_t *corpo, size_t n,
                            PhBuffer *risposta, PhErrore *e)
{
    if (c == NULL)
        return ph_errore(e, "componente mancante");
    /* Questi tipi li gestisce il componente da solo: mandarli a mano rompe il protocollo. */
    if ((tipo >= PH_MSG_SALUTO && tipo <= PH_MSG_CHIUDI) || (tipo >= 0x40 && tipo <= 0x4f) ||
        (tipo >= 0x50 && tipo <= 0x5f))
        return ph_errore(e, "la richiesta 0x%02x e' gestita dal componente, non si manda a mano", tipo);
    if (corpo == NULL && n > 0)
        return ph_errore(e, "corpo della richiesta mancante");
    PhBuffer msg = PH_BUFFER_VUOTO;
    if (ph_msg_inizia(&msg, tipo) != 0 || ph_buffer_aggiungi(&msg, corpo, n) != 0 ||
        ph_msg_finisci(&msg) != 0) {
        ph_buffer_libera(&msg);
        return ph_errore(e, "richiesta 0x%02x non costruibile (memoria o corpo troppo lungo)", tipo);
    }
    int ris = ph_componente_richiedi(c, &msg, risposta, c->timeout_ms, e);
    ph_buffer_libera(&msg);
    return ris;
}

PhCanale *ph_componente_canale(PhComponente *c, const char *riga, PhErrore *e)
{
    if (c == NULL || riga == NULL || riga[0] == '\0' || strchr(riga, '\n') != NULL ||
        strlen(riga) > 60 || strcmp(riga, "comandi") == 0 || strcmp(riga, "input") == 0 ||
        strncmp(riga, "video:", 6) == 0) {
        /* comandi, input e video sono canali del componente stesso */
        ph_errore(e, "apertura canale: tipo di canale non valido o riservato al componente");
        return NULL;
    }
    if (atomic_load(&c->morto)) {
        ph_errore(e, "il servizio sul telefono non e' piu' raggiungibile");
        return NULL;
    }
    return apri_tipo(c->adb, riga, PH_TIMEOUT_PREDEFINITO_MS, e);
}

int ph_componente_ascolta(PhComponente *c, uint8_t da_tipo, uint8_t a_tipo, PhEventoGrezzo f,
                          void *utente, PhErrore *e)
{
    if (c == NULL)
        return ph_errore(e, "componente mancante");
    if (da_tipo > a_tipo)
        return ph_errore(e, "intervallo di eventi non valido");
    /* Risposte ed eventi video hanno la loro strada. */
    if ((da_tipo <= PH_MSG_OK && a_tipo >= PH_MSG_OK) || (da_tipo <= PH_MSG_ERRORE && a_tipo >= PH_MSG_ERRORE) ||
        (da_tipo <= PH_MSG_VIDEO_EVENTO && a_tipo >= PH_MSG_VIDEO_EVENTO))
        return ph_errore(e, "l'intervallo 0x%02x-0x%02x comprende tipi gestiti dal componente",
                         da_tipo, a_tipo);
    int ris = 0;
    pthread_mutex_lock(&c->m_asc);
    int posto = -1, uguale = -1;
    bool sovrapposto = false;
    for (int i = 0; i < 8; i++) {
        if (c->asc[i].usato && c->asc[i].da == da_tipo && c->asc[i].a == a_tipo)
            uguale = i;
        else if (!c->asc[i].usato && posto < 0)
            posto = i;
        else if (c->asc[i].usato && f != NULL && da_tipo <= c->asc[i].a && a_tipo >= c->asc[i].da)
            sovrapposto = true; /* tocca un altro intervallo senza coincidere */
    }
    if (sovrapposto) {
        pthread_mutex_unlock(&c->m_asc);
        return ph_errore(e, "l'intervallo 0x%02x-0x%02x si sovrappone a un altro ascoltatore",
                         da_tipo, a_tipo);
    }
    if (f == NULL) {
        if (uguale >= 0)
            c->asc[uguale].usato = false;
    } else if (uguale >= 0 || posto >= 0) {
        int k = uguale >= 0 ? uguale : posto;
        c->asc[k].usato = true;
        c->asc[k].da = da_tipo;
        c->asc[k].a = a_tipo;
        c->asc[k].f = f;
        c->asc[k].utente = utente;
    } else {
        ris = ph_errore(e, "troppi ascoltatori di eventi (massimo 8)");
    }
    pthread_mutex_unlock(&c->m_asc);
    return ris;
}
