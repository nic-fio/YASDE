/* Scoperta mDNS/DNS-SD scritta da zero (vedi scoperta.h). */
#include "scoperta/scoperta.h"

#include <arpa/inet.h>
#include <errno.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "scoperta/scoperta_interno.h"

#define SERV_CONNETTI "_adb-tls-connect._tcp.local"
#define SERV_ABBINA "_adb-tls-pairing._tcp.local"

#define TIPO_A 1
#define TIPO_PTR 12
#define TIPO_AAAA 28
#define TIPO_SRV 33

#define MAX_INTERFACCE 16
#define MAX_SALTI 16 /* puntatori di compressione seguiti per un nome */

/* ---------- parte pura: lettura dei pacchetti ---------- */

static uint16_t leggi16(const uint8_t *p)
{
    return (uint16_t)((p[0] << 8) | p[1]);
}

static uint32_t leggi32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

/* Legge un nome DNS (con compressione) che inizia a *off e lo scrive come
 * testo con i punti, senza punto finale. *off avanza oltre il nome nella sua
 * posizione originale (un puntatore lo chiude). Torna 0 o -1 se il nome e'
 * troncato, ha un'etichetta non valida, un ciclo di puntatori o e' troppo lungo.
 * Il tetto ai salti impedisce i cicli; ogni lettura e' controllata su lunghezza. */
static int leggi_nome(const uint8_t *p, size_t len, size_t *off, char *out, size_t cap)
{
    size_t pos = *off;
    size_t fine_originale = 0;
    bool saltato = false;
    int salti = 0;
    size_t n = 0;

    for (;;) {
        if (pos >= len)
            return -1;
        uint8_t c = p[pos];
        if (c == 0) {
            pos++;
            if (!saltato)
                fine_originale = pos;
            break;
        }
        if ((c & 0xC0) == 0xC0) {
            if (pos + 1 >= len)
                return -1;
            size_t dest = (size_t)(((c & 0x3F) << 8) | p[pos + 1]);
            if (!saltato) {
                fine_originale = pos + 2;
                saltato = true;
            }
            if (dest >= len || ++salti > MAX_SALTI)
                return -1;
            pos = dest;
            continue;
        }
        if ((c & 0xC0) != 0)
            return -1; /* tipi di etichetta riservati */
        size_t l = c;
        if (pos + 1 + l > len)
            return -1;
        /* 253 e' il massimo di un nome DNS in testo; cap tiene il terminatore. */
        if (n + l + 1 >= cap || n + l > 253)
            return -1;
        if (n > 0)
            out[n++] = '.';
        for (size_t i = 0; i < l; i++) {
            uint8_t b = p[pos + 1 + i];
            /* Punti e caratteri di controllo dentro un'etichetta renderebbero
             * ambiguo il nome in testo: li sostituiamo. */
            out[n++] = (b == '.' || b < 0x20 || b == 0x7F) ? '_' : (char)b;
        }
        pos += 1 + l;
    }
    out[n] = '\0';
    *off = fine_originale;
    return 0;
}

void ph_sc_raccolta_inizia(PhScRaccolta *r)
{
    memset(r, 0, sizeof *r);
}

static void aggiungi_addr(PhScRaccolta *r, const char *nome, const char *testo, bool v6)
{
    for (size_t i = 0; i < r->n_addr; i++)
        if (r->addr[i].v6 == v6 && strcasecmp(r->addr[i].nome, nome) == 0 &&
            strcmp(r->addr[i].indirizzo, testo) == 0)
            return;
    if (r->n_addr >= PH_SC_MAX_ADDR)
        return;
    PhScAddr *a = &r->addr[r->n_addr++];
    snprintf(a->nome, sizeof a->nome, "%s", nome);
    snprintf(a->indirizzo, sizeof a->indirizzo, "%s", testo);
    a->v6 = v6;
}

static void aggiungi_srv(PhScRaccolta *r, const char *prop, const char *bers, uint16_t porta,
                         const char *origine)
{
    for (size_t i = 0; i < r->n_srv; i++)
        if (strcasecmp(r->srv[i].proprietario, prop) == 0 && r->srv[i].porta == porta &&
            strcasecmp(r->srv[i].bersaglio, bers) == 0)
            return;
    if (r->n_srv >= PH_SC_MAX_SRV)
        return;
    PhScSrv *s = &r->srv[r->n_srv++];
    snprintf(s->proprietario, sizeof s->proprietario, "%s", prop);
    snprintf(s->bersaglio, sizeof s->bersaglio, "%s", bers);
    snprintf(s->origine, sizeof s->origine, "%s", origine != NULL ? origine : "");
    s->porta = porta;
}

/* Un record con TTL 0 e' un "addio": il telefono toglie il servizio (per
 * esempio la porta di abbinamento appena chiusa). Si toglie anche da noi. */
static void togli_srv(PhScRaccolta *r, const char *proprietario, const char *bersaglio,
                      uint16_t porta)
{
    size_t j = 0;
    /* Solo l'SRV identico: l'addio della porta vecchia, arrivato dopo
     * l'annuncio della nuova, non deve toglierla. */
    for (size_t i = 0; i < r->n_srv; i++)
        if (strcasecmp(r->srv[i].proprietario, proprietario) != 0 || r->srv[i].porta != porta ||
            strcasecmp(r->srv[i].bersaglio, bersaglio) != 0)
            r->srv[j++] = r->srv[i];
    r->n_srv = j;
}

static void togli_ptr(PhScRaccolta *r, const char *istanza)
{
    size_t j = 0;
    for (size_t i = 0; i < r->n_ptr; i++)
        if (strcasecmp(r->ptr[i], istanza) != 0)
            memcpy(r->ptr[j++], r->ptr[i], PH_SC_NOME);
    r->n_ptr = j;
}

static void aggiungi_ptr(PhScRaccolta *r, const char *istanza)
{
    for (size_t i = 0; i < r->n_ptr; i++)
        if (strcasecmp(r->ptr[i], istanza) == 0)
            return;
    if (r->n_ptr < PH_SC_MAX_PTR)
        snprintf(r->ptr[r->n_ptr++], PH_SC_NOME, "%s", istanza);
}

void ph_sc_raccolta_aggiungi(PhScRaccolta *r, const uint8_t *p, size_t len, const char *origine)
{
    if (p == NULL || len < 12)
        return;
    uint16_t flags = leggi16(p + 2);
    if ((flags & 0x8000) == 0)
        return; /* e' una domanda di qualcun altro */
    size_t qd = leggi16(p + 4);
    size_t record = (size_t)leggi16(p + 6) + leggi16(p + 8) + leggi16(p + 10);
    size_t off = 12;
    char nome[PH_SC_NOME];

    for (size_t i = 0; i < qd; i++) {
        if (leggi_nome(p, len, &off, nome, sizeof nome) != 0 || off + 4 > len)
            return;
        off += 4;
    }
    for (size_t i = 0; i < record; i++) {
        if (leggi_nome(p, len, &off, nome, sizeof nome) != 0)
            return;
        if (off + 10 > len)
            return;
        uint16_t tipo = leggi16(p + off);
        uint16_t classe = (uint16_t)(leggi16(p + off + 2) & 0x7FFF);
        uint32_t ttl = leggi32(p + off + 4);
        size_t rdlen = leggi16(p + off + 8);
        off += 10;
        if (rdlen > len - off)
            return;
        size_t fine = off + rdlen;
        if (classe == 1) {
            if (tipo == TIPO_PTR) {
                size_t o = off;
                char istanza[PH_SC_NOME];
                if (leggi_nome(p, len, &o, istanza, sizeof istanza) == 0 && o <= fine &&
                    (strcasecmp(nome, SERV_CONNETTI) == 0 || strcasecmp(nome, SERV_ABBINA) == 0)) {
                    if (ttl > 0)
                        aggiungi_ptr(r, istanza);
                    else
                        togli_ptr(r, istanza);
                }
            } else if (tipo == TIPO_SRV && rdlen >= 7) {
                size_t o = off + 6;
                char bers[PH_SC_NOME];
                /* Il bersaglio puo' essere compresso con un puntatore verso
                 * il resto del pacchetto, quindi il limite di lettura e' len. */
                if (leggi_nome(p, len, &o, bers, sizeof bers) == 0 && o <= fine) {
                    if (ttl > 0)
                        aggiungi_srv(r, nome, bers, leggi16(p + off + 4), origine);
                    else
                        togli_srv(r, nome, bers, leggi16(p + off + 4));
                }
            } else if (tipo == TIPO_A && rdlen == 4 && ttl > 0) {
                char t[INET_ADDRSTRLEN];
                if (inet_ntop(AF_INET, p + off, t, sizeof t) != NULL)
                    aggiungi_addr(r, nome, t, false);
            } else if (tipo == TIPO_AAAA && rdlen == 16 && ttl > 0) {
                char t[INET6_ADDRSTRLEN];
                if (inet_ntop(AF_INET6, p + off, t, sizeof t) != NULL)
                    aggiungi_addr(r, nome, t, true);
            }
        }
        off = fine;
    }
}

/* Vero se `s` finisce con ".suffisso" (senza distinzione tra maiuscole);
 * *lunghezza_prefisso e' la lunghezza di cio' che precede il punto. */
static bool finisce_con(const char *s, const char *suffisso, size_t *lunghezza_prefisso)
{
    size_t ls = strlen(s), lf = strlen(suffisso);
    if (ls <= lf + 1 || s[ls - lf - 1] != '.' || strcasecmp(s + ls - lf, suffisso) != 0)
        return false;
    *lunghezza_prefisso = ls - lf - 1;
    return true;
}

size_t ph_sc_raccolta_risolvi(const PhScRaccolta *r, PhTelefonoTrovato *trovati, size_t *numero,
                              size_t massimo)
{
    size_t aggiunti = 0;
    for (size_t i = 0; i < r->n_srv; i++) {
        const PhScSrv *s = &r->srv[i];
        size_t lp;
        bool abbina;
        if (finisce_con(s->proprietario, SERV_CONNETTI, &lp))
            abbina = false;
        else if (finisce_con(s->proprietario, SERV_ABBINA, &lp))
            abbina = true;
        else
            continue;
        if (s->porta == 0 || lp == 0)
            continue;

        /* Meglio un IPv4 (gli IPv6 link-local non si usano senza scope);
         * se manca ogni record A/AAAA si ripiega sull'origine del pacchetto. */
        const char *ind = NULL;
        for (int v6 = 0; v6 < 2 && ind == NULL; v6++)
            for (size_t k = 0; k < r->n_addr; k++)
                if (r->addr[k].v6 == (v6 == 1) && strcasecmp(r->addr[k].nome, s->bersaglio) == 0) {
                    ind = r->addr[k].indirizzo;
                    break;
                }
        if (ind == NULL && s->origine[0] != '\0')
            ind = s->origine;
        if (ind == NULL)
            continue;

        char nome_voce[sizeof trovati[0].nome];
        size_t n = lp < sizeof nome_voce - 1 ? lp : sizeof nome_voce - 1;
        memcpy(nome_voce, s->proprietario, n);
        nome_voce[n] = '\0';
        bool doppio = false;
        for (size_t k = 0; k < *numero && !doppio; k++)
            doppio = trovati[k].abbinamento == abbina && trovati[k].porta == s->porta &&
                     strcmp(trovati[k].indirizzo, ind) == 0 &&
                     strcasecmp(trovati[k].nome, nome_voce) == 0;
        if (doppio || *numero >= massimo)
            continue;
        PhTelefonoTrovato *t = &trovati[(*numero)++];
        memset(t, 0, sizeof *t);
        memcpy(t->nome, nome_voce, n + 1);
        snprintf(t->indirizzo, sizeof t->indirizzo, "%s", ind);
        t->porta = s->porta;
        t->abbinamento = abbina;
        aggiunti++;
    }
    return aggiunti;
}

size_t ph_scoperta_leggi_risposta(const uint8_t *pacchetto, size_t lunghezza,
                                  PhTelefonoTrovato *trovati, size_t *numero, size_t massimo)
{
    if (pacchetto == NULL || trovati == NULL || numero == NULL || *numero > massimo)
        return 0;
    PhScRaccolta *r = calloc(1, sizeof *r);
    if (r == NULL)
        return 0;
    ph_sc_raccolta_aggiungi(r, pacchetto, lunghezza, NULL);
    size_t n = ph_sc_raccolta_risolvi(r, trovati, numero, massimo);
    free(r);
    return n;
}

/* ---------- domanda ---------- */

/* Scrive un nome come etichette; torna i byte scritti o 0 se non ci stanno. */
static size_t scrivi_nome(uint8_t *out, size_t cap, const char *nome)
{
    size_t n = 0;
    while (*nome != '\0') {
        const char *punto = strchr(nome, '.');
        size_t l = punto != NULL ? (size_t)(punto - nome) : strlen(nome);
        if (l == 0 || l > 63 || n + 1 + l + 1 > cap)
            return 0;
        out[n++] = (uint8_t)l;
        memcpy(out + n, nome, l);
        n += l;
        nome += l + (punto != NULL ? 1 : 0);
    }
    if (n + 1 > cap)
        return 0;
    out[n++] = 0;
    return n;
}

static size_t scrivi_domanda(uint8_t *out, size_t cap, const char *nome, uint16_t tipo, bool qu)
{
    size_t n = scrivi_nome(out, cap, nome);
    if (n == 0 || n + 4 > cap)
        return 0;
    out[n++] = (uint8_t)(tipo >> 8);
    out[n++] = (uint8_t)tipo;
    /* Classe IN; il bit alto chiede la risposta in unicast (QU). */
    out[n++] = qu ? 0x80 : 0x00;
    out[n++] = 0x01;
    return n;
}

size_t ph_scoperta_domanda(uint8_t *pacchetto, size_t capacita, bool prima)
{
    if (pacchetto == NULL || capacita < 12)
        return 0;
    memset(pacchetto, 0, 12);
    pacchetto[5] = 2; /* due domande */
    size_t n = 12;
    const char *servizi[2] = {SERV_CONNETTI, SERV_ABBINA};
    for (int i = 0; i < 2; i++) {
        size_t k = scrivi_domanda(pacchetto + n, capacita - n, servizi[i], TIPO_PTR, prima);
        if (k == 0)
            return 0;
        n += k;
    }
    return n;
}

/* Vero se `sorgente` sta nella sottorete di una delle interfacce. Tutti gli
 * indirizzi sono in ordine di rete. Serve a scartare i pacchetti sulla 5353
 * che arrivano da fuori della rete locale (il multicast non e' instradato,
 * quindi un mittente esterno non e' un vicino vero). */
bool ph_sc_in_sottorete(uint32_t sorgente, const uint32_t *indirizzi, const uint32_t *maschere,
                        size_t n)
{
    for (size_t i = 0; i < n; i++)
        if (((sorgente ^ indirizzi[i]) & maschere[i]) == 0)
            return true;
    return false;
}

/* ---------- rete ---------- */

typedef struct {
    int fd;
    struct in_addr indirizzo; /* della sua interfaccia, per mandare */
    bool puo_mandare;
} Presa;

static int64_t ora_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (int64_t)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

static int presa_ricevi(const struct in_addr *ifaddr_list, size_t n_if, PhErrore *e, int *fd_out)
{
    int fd = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    if (fd < 0)
        return ph_errore(e, "scoperta: socket di ascolto non creato: %s", strerror(errno));
    int uno = 1;
    /* La porta 5353 puo' essere gia' di avahi: la condividiamo. */
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &uno, sizeof uno);
#ifdef SO_REUSEPORT
    setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &uno, sizeof uno);
#endif
    struct sockaddr_in a = {.sin_family = AF_INET, .sin_port = htons(5353)};
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(fd, (struct sockaddr *)&a, sizeof a) < 0) {
        int err = errno;
        close(fd);
        return ph_errore(e, "scoperta: porta 5353 non disponibile: %s", strerror(err));
    }
    size_t iscritte = 0;
    for (size_t i = 0; i < n_if; i++) {
        struct ip_mreq m;
        m.imr_multiaddr.s_addr = inet_addr("224.0.0.251");
        m.imr_interface = ifaddr_list[i];
        if (setsockopt(fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &m, sizeof m) == 0)
            iscritte++;
    }
    if (iscritte == 0) {
        close(fd);
        return ph_errore(e, "scoperta: nessuna interfaccia ha accettato il gruppo multicast");
    }
    *fd_out = fd;
    return 0;
}

/* Una presa di invio per interfaccia: legata all'indirizzo dell'interfaccia e
 * a una porta qualsiasi. Le risposte unicast (bit QU o risposta "legacy" a una
 * domanda da porta diversa da 5353) tornano proprio qui. */
static int presa_invio(struct in_addr ind, Presa *p)
{
    int fd = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    if (fd < 0)
        return -1;
    int ttl = 255, zero = 0;
    struct sockaddr_in a = {.sin_family = AF_INET};
    a.sin_addr = ind;
    if (bind(fd, (struct sockaddr *)&a, sizeof a) < 0 ||
        setsockopt(fd, IPPROTO_IP, IP_MULTICAST_IF, &ind, sizeof ind) < 0) {
        close(fd);
        return -1;
    }
    setsockopt(fd, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof ttl);
    setsockopt(fd, IPPROTO_IP, IP_MULTICAST_LOOP, &zero, sizeof zero);
    p->fd = fd;
    p->indirizzo = ind;
    p->puo_mandare = true;
    return 0;
}

static void manda_a_tutti(Presa *prese, size_t n, const uint8_t *pkt, size_t len, FILE *traccia,
                          int64_t t0)
{
    struct sockaddr_in dest = {.sin_family = AF_INET, .sin_port = htons(5353)};
    dest.sin_addr.s_addr = inet_addr("224.0.0.251");
    for (size_t i = 0; i < n; i++) {
        ssize_t k = sendto(prese[i].fd, pkt, len, MSG_NOSIGNAL, (struct sockaddr *)&dest, sizeof dest);
        if (traccia != NULL) {
            char t[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &prese[i].indirizzo, t, sizeof t);
            fprintf(traccia, "[%5lld ms] domanda di %zu byte da %s: %s\n",
                    (long long)(ora_ms() - t0), len, t, k < 0 ? strerror(errno) : "ok");
        }
    }
}

/* Domande di approfondimento: per un'istanza vista in un PTR senza SRV si
 * chiede l'SRV, per un bersaglio senza A si chiede l'A. */
static void approfondisci(const PhScRaccolta *r, Presa *prese, size_t n_prese, FILE *traccia,
                          int64_t t0)
{
    uint8_t pkt[512];
    for (size_t i = 0; i < r->n_ptr; i++) {
        bool ha = false;
        for (size_t k = 0; k < r->n_srv && !ha; k++)
            ha = strcasecmp(r->srv[k].proprietario, r->ptr[i]) == 0;
        if (ha)
            continue;
        memset(pkt, 0, 12);
        pkt[5] = 1;
        size_t n = scrivi_domanda(pkt + 12, sizeof pkt - 12, r->ptr[i], TIPO_SRV, false);
        if (n > 0)
            manda_a_tutti(prese, n_prese, pkt, 12 + n, traccia, t0);
    }
    for (size_t i = 0; i < r->n_srv; i++) {
        bool ha = false;
        for (size_t k = 0; k < r->n_addr && !ha; k++)
            ha = strcasecmp(r->addr[k].nome, r->srv[i].bersaglio) == 0;
        if (ha)
            continue;
        memset(pkt, 0, 12);
        pkt[5] = 1;
        size_t n = scrivi_domanda(pkt + 12, sizeof pkt - 12, r->srv[i].bersaglio, TIPO_A, false);
        if (n > 0)
            manda_a_tutti(prese, n_prese, pkt, 12 + n, traccia, t0);
    }
}

int ph_scoperta_cerca_traccia(int durata_ms, PhTelefonoTrovato *trovati, size_t massimo,
                              PhErrore *e, FILE *traccia)
{
    if (trovati == NULL || massimo == 0 || durata_ms < 0)
        return ph_errore(e, "scoperta: parametri non validi");

    struct ifaddrs *lista = NULL;
    if (getifaddrs(&lista) < 0)
        return ph_errore(e, "scoperta: elenco interfacce non letto: %s", strerror(errno));

    Presa prese[MAX_INTERFACCE + 1];
    struct in_addr indirizzi[MAX_INTERFACCE];
    uint32_t maschere[MAX_INTERFACCE];
    uint32_t ind_rete[MAX_INTERFACCE];
    size_t n_if = 0;
    for (struct ifaddrs *i = lista; i != NULL && n_if < MAX_INTERFACCE; i = i->ifa_next) {
        if (i->ifa_addr == NULL || i->ifa_addr->sa_family != AF_INET)
            continue;
        unsigned f = i->ifa_flags;
        if (!(f & IFF_UP) || !(f & IFF_RUNNING) || (f & IFF_LOOPBACK) || !(f & IFF_MULTICAST))
            continue;
        struct in_addr ind = ((struct sockaddr_in *)(void *)i->ifa_addr)->sin_addr;
        bool doppia = false;
        for (size_t k = 0; k < n_if; k++)
            doppia = doppia || indirizzi[k].s_addr == ind.s_addr;
        if (!doppia) {
            uint32_t mask = 0xFFFFFFFFu;
            if (i->ifa_netmask != NULL && i->ifa_netmask->sa_family == AF_INET)
                mask = ((struct sockaddr_in *)(void *)i->ifa_netmask)->sin_addr.s_addr;
            maschere[n_if] = mask;
            ind_rete[n_if] = ind.s_addr;
            indirizzi[n_if++] = ind;
        }
    }
    freeifaddrs(lista);
    if (n_if == 0)
        return ph_errore(e, "scoperta: nessuna interfaccia di rete attiva con multicast");

    size_t n_prese = 0;
    for (size_t i = 0; i < n_if; i++)
        if (presa_invio(indirizzi[i], &prese[n_prese]) == 0)
            n_prese++;
    if (n_prese == 0)
        return ph_errore(e, "scoperta: nessuna presa di invio creata");

    /* L'ascolto sulla 5353 e' un di piu': se la porta non c'e' si va avanti
     * con le sole risposte unicast, ma si dice in traccia. */
    int fd_multicast = -1;
    PhErrore e2;
    e2.testo[0] = '\0';
    if (presa_ricevi(indirizzi, n_if, &e2, &fd_multicast) != 0 && traccia != NULL)
        fprintf(traccia, "%s\n", e2.testo);

    struct pollfd pfd[MAX_INTERFACCE + 1];
    PhScRaccolta *raccolta = malloc(sizeof *raccolta);
    PhTelefonoTrovato *precedenti = malloc(massimo * sizeof *precedenti);
    if (raccolta == NULL || precedenti == NULL) {
        free(raccolta);
        free(precedenti);
        for (size_t i = 0; i < n_prese; i++)
            close(prese[i].fd);
        if (fd_multicast >= 0)
            close(fd_multicast);
        return ph_errore(e, "scoperta: memoria esaurita");
    }
    ph_sc_raccolta_inizia(raccolta);

    /* Intervalli crescenti (ms dall'inizio): un telefono col Wi-Fi in
     * risparmio sente la domanda solo al risveglio successivo. Poi ogni 2 s. */
    static const int piano[] = {0, 250, 1000, 2000, 3000, 4500, 6000, 8000};
    size_t n_piano = sizeof piano / sizeof piano[0];
    size_t prossimo = 0;
    size_t numero = 0;
    int64_t t0 = ora_ms();
    uint8_t pkt[512];
    uint8_t ric[9000];

    for (;;) {
        int64_t adesso = ora_ms() - t0;
        if (adesso >= durata_ms)
            break;
        int64_t scadenza = prossimo < n_piano ? piano[prossimo] : piano[n_piano - 1] + (int64_t)(prossimo - n_piano + 1) * 2000;
        if (adesso >= scadenza) {
            size_t l = ph_scoperta_domanda(pkt, sizeof pkt, prossimo == 0);
            if (l > 0)
                manda_a_tutti(prese, n_prese, pkt, l, traccia, t0);
            if (prossimo > 0)
                approfondisci(raccolta, prese, n_prese, traccia, t0);
            prossimo++;
            continue;
        }
        int64_t attesa = scadenza - adesso;
        if (durata_ms - adesso < attesa)
            attesa = durata_ms - adesso;

        size_t np = 0;
        for (size_t i = 0; i < n_prese; i++)
            pfd[np++] = (struct pollfd){.fd = prese[i].fd, .events = POLLIN};
        if (fd_multicast >= 0)
            pfd[np++] = (struct pollfd){.fd = fd_multicast, .events = POLLIN};
        int pr = poll(pfd, (nfds_t)np, (int)attesa);
        if (pr < 0 && errno != EINTR) {
            int err = errno;
            free(precedenti);
            free(raccolta);
            for (size_t i = 0; i < n_prese; i++)
                close(prese[i].fd);
            if (fd_multicast >= 0)
                close(fd_multicast);
            return ph_errore(e, "scoperta: attesa dei pacchetti fallita: %s", strerror(err));
        }
        if (pr <= 0)
            continue;
        for (size_t i = 0; i < np; i++) {
            if (!(pfd[i].revents & POLLIN))
                continue;
            /* Tetto di pacchetti per presa e per giro, e controllo del tempo:
             * una raffica sulla rete non deve tenerci oltre durata_ms. */
            for (int quanti = 0; quanti < 64; quanti++) {
                if (ora_ms() - t0 >= durata_ms)
                    break;
                struct sockaddr_in da;
                socklen_t dl = sizeof da;
                ssize_t k = recvfrom(pfd[i].fd, ric, sizeof ric, 0, (struct sockaddr *)&da, &dl);
                if (k < 0)
                    break; /* EAGAIN: svuotata */
                char orig[INET_ADDRSTRLEN] = "";
                inet_ntop(AF_INET, &da.sin_addr, orig, sizeof orig);
                if (!ph_sc_in_sottorete(da.sin_addr.s_addr, ind_rete, maschere, n_if)) {
                    if (traccia != NULL)
                        fprintf(traccia, "[%5lld ms] scartato pacchetto da %s: fuori dalla rete locale\n",
                                (long long)(ora_ms() - t0), orig);
                    continue;
                }
                size_t prima = raccolta->n_srv;
                ph_sc_raccolta_aggiungi(raccolta, ric, (size_t)k, orig);
                if (traccia != NULL)
                    fprintf(traccia, "[%5lld ms] pacchetto di %zd byte da %s (%s), SRV nuovi: %zu\n",
                            (long long)(ora_ms() - t0), k, orig,
                            pfd[i].fd == fd_multicast ? "multicast" : "unicast", raccolta->n_srv - prima);
            }
        }
        /* Si ricalcola l'elenco da zero a ogni giro: cosi' un "addio" fa sparire
         * una voce gia' trovata. Alla traccia si dicono solo le voci nuove. */
        size_t vecchi = numero;
        memcpy(precedenti, trovati, vecchi * sizeof *trovati);
        numero = 0;
        ph_sc_raccolta_risolvi(raccolta, trovati, &numero, massimo);
        for (size_t k = 0; k < numero && traccia != NULL; k++) {
            bool nuovo = true;
            for (size_t j = 0; j < vecchi && nuovo; j++)
                nuovo = !(trovati[k].porta == precedenti[j].porta &&
                          trovati[k].abbinamento == precedenti[j].abbinamento &&
                          strcmp(trovati[k].nome, precedenti[j].nome) == 0 &&
                          strcmp(trovati[k].indirizzo, precedenti[j].indirizzo) == 0);
            if (nuovo)
                fprintf(traccia, "[%5lld ms] trovato %s %s:%u %s\n", (long long)(ora_ms() - t0),
                        trovati[k].nome, trovati[k].indirizzo, trovati[k].porta,
                        trovati[k].abbinamento ? "abbinamento" : "collegamento");
        }
    }

    free(precedenti);
    free(raccolta);
    for (size_t i = 0; i < n_prese; i++)
        close(prese[i].fd);
    if (fd_multicast >= 0)
        close(fd_multicast);
    return (int)numero;
}

int ph_scoperta_cerca(int durata_ms, PhTelefonoTrovato *trovati, size_t massimo, PhErrore *e)
{
    return ph_scoperta_cerca_traccia(durata_ms, trovati, massimo, e, NULL);
}
