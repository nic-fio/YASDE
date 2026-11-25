/* Strumento di prova a riga di comando: usa solo i contratti dei pezzi
 * (scoperta, adb, abbinamento, file, componente) per provarli sul telefono
 * vero senza la shell grafica. Non e' parte dell'app.
 * Ogni errore: la frase di PhErrore su stderr e uscita 1. */
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "abbinamento/abbinamento.h"
#include "adb/adb.h"
#include "componente/componente.h"
#include "file/file.h"
#include "scoperta/scoperta.h"

#define TEMPO_CONNESSIONE_MS 15000

static const char *g_chiave = NULL;    /* --chiave */
static const char *g_telefono = NULL;  /* --telefono HOST:PORTA */

static int fallisci(const PhErrore *e, const char *predefinita)
{
    fprintf(stderr, "errore: %s\n", e->testo[0] != '\0' ? e->testo : predefinita);
    return 1;
}

static double ora_s(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + (double)t.tv_nsec / 1e9;
}

/* "host:porta" -> host (copiato in buf) e porta. Per gli IPv6 vale l'ultimo ':'. */
static int dividi_host_porta(const char *s, char *host, size_t cap, uint16_t *porta)
{
    const char *due = strrchr(s, ':');
    if (due == NULL || due == s || (size_t)(due - s) >= cap)
        return -1;
    char *fine = NULL;
    long p = strtol(due + 1, &fine, 10);
    if (*(due + 1) == '\0' || *fine != '\0' || p <= 0 || p > 65535)
        return -1;
    memcpy(host, s, (size_t)(due - s));
    host[due - s] = '\0';
    *porta = (uint16_t)p;
    return 0;
}

static PhChiave *carica_chiave(PhErrore *e)
{
    char percorso[1024];
    if (g_chiave != NULL) {
        snprintf(percorso, sizeof percorso, "%s", g_chiave);
    } else {
        const char *casa = getenv("HOME");
        if (casa == NULL) {
            ph_errore(e, "variabile HOME assente: indicare la chiave con --chiave");
            return NULL;
        }
        snprintf(percorso, sizeof percorso, "%s/.config/YASDE/chiave.pem", casa);
    }
    return ph_chiave_carica_o_crea(percorso, e);
}

/* Collega al telefono indicato o al primo trovato dalla scoperta. */
static PhAdb *collega(const PhChiave *chiave, PhErrore *e)
{
    char host[64];
    uint16_t porta = 0;
    if (g_telefono != NULL) {
        if (dividi_host_porta(g_telefono, host, sizeof host, &porta) != 0) {
            ph_errore(e, "telefono non valido: serve HOST:PORTA");
            return NULL;
        }
    } else {
        PhTelefonoTrovato t[16];
        int n = ph_scoperta_cerca(5000, t, 16, e);
        if (n < 0)
            return NULL;
        int scelto = -1;
        for (int i = 0; i < n && scelto < 0; i++)
            if (!t[i].abbinamento)
                scelto = i;
        if (scelto < 0) {
            ph_errore(e, "nessun telefono con il debug wireless trovato in rete");
            return NULL;
        }
        snprintf(host, sizeof host, "%s", t[scelto].indirizzo);
        porta = t[scelto].porta;
        fprintf(stderr, "uso il telefono %s\n", t[scelto].nome);
    }
    bool non_abbinato = false;
    PhAdb *a = ph_adb_collega(host, porta, chiave, TEMPO_CONNESSIONE_MS, &non_abbinato, e);
    if (a == NULL && non_abbinato)
        ph_errore_contesto(e, "PC non abbinato: usare il comando abbina");
    return a;
}

/* ---- comandi ---- */

static int cmd_cerca(int argc, char **argv)
{
    int secondi = argc > 0 ? atoi(argv[0]) : 5;
    if (secondi <= 0)
        secondi = 5;
    PhTelefonoTrovato t[32];
    PhErrore e = {{0}};
    int n = ph_scoperta_cerca(secondi * 1000, t, 32, &e);
    if (n < 0)
        return fallisci(&e, "scoperta fallita");
    for (int i = 0; i < n; i++)
        printf("%s %s:%u %s\n", t[i].nome, t[i].indirizzo, t[i].porta,
               t[i].abbinamento ? "abbinamento" : "collegamento");
    printf("trovati: %d\n", n);
    return 0;
}

static int cmd_abbina(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "uso: abbina HOST:PORTA CODICE\n");
        return 1;
    }
    char host[64];
    uint16_t porta;
    PhErrore e = {{0}};
    if (dividi_host_porta(argv[0], host, sizeof host, &porta) != 0) {
        fprintf(stderr, "errore: serve HOST:PORTA\n");
        return 1;
    }
    PhChiave *k = carica_chiave(&e);
    if (k == NULL)
        return fallisci(&e, "chiave non disponibile");
    int r = ph_abbinamento_esegui(host, porta, argv[1], k, TEMPO_CONNESSIONE_MS, &e);
    ph_chiave_libera(k);
    if (r != 0)
        return fallisci(&e, "abbinamento fallito");
    printf("abbinamento riuscito\n");
    return 0;
}

static int cmd_shell(int argc, char **argv)
{
    if (argc < 1) {
        fprintf(stderr, "uso: shell COMANDO...\n");
        return 1;
    }
    PhErrore e = {{0}};
    PhBuffer cmd = PH_BUFFER_VUOTO;
    for (int i = 0; i < argc; i++) {
        if ((i > 0 && ph_buffer_aggiungi(&cmd, " ", 1) != 0) ||
            ph_buffer_aggiungi(&cmd, argv[i], strlen(argv[i])) != 0) {
            ph_buffer_libera(&cmd);
            fprintf(stderr, "errore: memoria esaurita\n");
            return 1;
        }
    }
    PhChiave *k = carica_chiave(&e);
    PhAdb *a = k != NULL ? collega(k, &e) : NULL;
    if (a == NULL) {
        ph_buffer_libera(&cmd);
        if (k != NULL)
            ph_chiave_libera(k);
        return fallisci(&e, "collegamento fallito");
    }
    PhBuffer out = PH_BUFFER_VUOTO, err = PH_BUFFER_VUOTO;
    int codice = 0;
    int r = ph_shell_esegui(a, (const char *)cmd.dati, &out, &err, &codice, 60000, &e);
    int uscita = 1;
    if (r != 0) {
        fallisci(&e, "comando non eseguito");
    } else {
        if (out.lunghezza > 0)
            fwrite(out.dati, 1, out.lunghezza, stdout);
        if (err.lunghezza > 0)
            fwrite(err.dati, 1, err.lunghezza, stderr);
        uscita = codice;
    }
    ph_buffer_libera(&out);
    ph_buffer_libera(&err);
    ph_buffer_libera(&cmd);
    ph_adb_chiudi(a);
    ph_chiave_libera(k);
    return uscita;
}

static void progresso(uint64_t fatti, uint64_t totale, void *utente)
{
    int *ultimo = utente;
    int perc = totale > 0 ? (int)(fatti * 100 / totale) : 100;
    if (perc != *ultimo) {
        *ultimo = perc;
        fprintf(stderr, "\r%3d%%", perc);
        fflush(stderr);
    }
}

static int cmd_copia(bool invia, int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, invia ? "uso: invia LOCALE REMOTO\n" : "uso: ricevi REMOTO LOCALE\n");
        return 1;
    }
    PhErrore e = {{0}};
    PhChiave *k = carica_chiave(&e);
    PhAdb *a = k != NULL ? collega(k, &e) : NULL;
    if (a == NULL) {
        if (k != NULL)
            ph_chiave_libera(k);
        return fallisci(&e, "collegamento fallito");
    }
    int ultimo = -1;
    double t0 = ora_s();
    PhFileEsito r = invia ? ph_file_invia(a, argv[0], argv[1], progresso, &ultimo, NULL, &e)
                          : ph_file_ricevi(a, argv[0], argv[1], progresso, &ultimo, NULL, &e);
    double dt = ora_s() - t0;
    int uscita = 0;
    if (ultimo >= 0)
        fprintf(stderr, "\n");
    if (r != PH_FILE_RIUSCITO) {
        uscita = fallisci(&e, "copia fallita");
    } else {
        /* Per la velocita' serve la dimensione: si legge dal lato locale o remoto. */
        PhFileVoce v;
        uint64_t byte = 0;
        PhErrore e2 = {{0}};
        if (invia) {
            FILE *f = fopen(argv[0], "rb");
            if (f != NULL) {
                if (fseek(f, 0, SEEK_END) == 0)
                    byte = (uint64_t)ftell(f);
                fclose(f);
            }
        } else if (ph_file_info(a, argv[0], &v, &e2) == PH_FILE_RIUSCITO) {
            byte = v.dimensione;
        }
        printf("copiati %llu byte in %.2f s (%.1f MB/s)\n", (unsigned long long)byte, dt,
               dt > 0 ? (double)byte / dt / 1e6 : 0.0);
    }
    ph_adb_chiudi(a);
    ph_chiave_libera(k);
    return uscita;
}

static int cmd_elenca(int argc, char **argv)
{
    if (argc != 1) {
        fprintf(stderr, "uso: elenca CARTELLA\n");
        return 1;
    }
    PhErrore e = {{0}};
    PhChiave *k = carica_chiave(&e);
    PhAdb *a = k != NULL ? collega(k, &e) : NULL;
    if (a == NULL) {
        if (k != NULL)
            ph_chiave_libera(k);
        return fallisci(&e, "collegamento fallito");
    }
    PhFileElenco el = {NULL, 0};
    int uscita = 0;
    if (ph_file_elenca(a, argv[0], &el, &e) != PH_FILE_RIUSCITO) {
        uscita = fallisci(&e, "elenco fallito");
    } else {
        for (size_t i = 0; i < el.numero; i++)
            printf("%c %12llu %lld %s\n", el.voci[i].cartella ? 'd' : el.voci[i].collegamento ? 'l' : '-',
                   (unsigned long long)el.voci[i].dimensione, (long long)el.voci[i].modificato,
                   el.voci[i].nome);
        ph_file_elenco_libera(&el);
    }
    ph_adb_chiudi(a);
    ph_chiave_libera(k);
    return uscita;
}

static int cmd_info(int argc, char **argv)
{
    if (argc != 1) {
        fprintf(stderr, "uso: info PERCORSO\n");
        return 1;
    }
    PhErrore e = {{0}};
    PhChiave *k = carica_chiave(&e);
    PhAdb *a = k != NULL ? collega(k, &e) : NULL;
    if (a == NULL) {
        if (k != NULL)
            ph_chiave_libera(k);
        return fallisci(&e, "collegamento fallito");
    }
    PhFileVoce v;
    int uscita = 0;
    if (ph_file_info(a, argv[0], &v, &e) != PH_FILE_RIUSCITO)
        uscita = fallisci(&e, "info fallita");
    else
        printf("%s %llu byte, modificato %lld\n",
               v.cartella ? "cartella" : v.collegamento ? "collegamento" : "file",
               (unsigned long long)v.dimensione, (long long)v.modificato);
    ph_adb_chiudi(a);
    ph_chiave_libera(k);
    return uscita;
}

static void evento(uint32_t id, uint8_t che_cosa, uint16_t a, uint16_t b, void *utente)
{
    (void)utente;
    fprintf(stderr, "evento: video %u tipo %u (%u, %u)\n", id, che_cosa, a, b);
}

static int cmd_video(int argc, char **argv)
{
    if (argc < 5) {
        fprintf(stderr, "uso: video JAR PACCHETTO LARGHEZZAxALTEZZA SECONDI USCITA.h264 "
                        "[--dpi N] [--tocco X,Y]\n");
        return 1;
    }
    const char *jar = argv[0], *pacchetto = argv[1], *uscita_file = argv[4];
    unsigned w = 0, h = 0;
    if (sscanf(argv[2], "%ux%u", &w, &h) != 2 || w > 65535 || h > 65535) {
        fprintf(stderr, "errore: dimensione non valida, serve LARGHEZZAxALTEZZA\n");
        return 1;
    }
    int secondi = atoi(argv[3]);
    if (secondi <= 0) {
        fprintf(stderr, "errore: SECONDI non valido\n");
        return 1;
    }
    int dpi = -1;
    bool tocco = false;
    unsigned tx = 0, ty = 0;
    for (int i = 5; i < argc; i++) {
        if (strcmp(argv[i], "--dpi") == 0 && i + 1 < argc) {
            dpi = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--tocco") == 0 && i + 1 < argc) {
            if (sscanf(argv[++i], "%u,%u", &tx, &ty) != 2 || tx > 65535 || ty > 65535) {
                fprintf(stderr, "errore: --tocco vuole X,Y\n");
                return 1;
            }
            tocco = true;
        } else {
            fprintf(stderr, "errore: opzione sconosciuta: %s\n", argv[i]);
            return 1;
        }
    }

    PhErrore e = {{0}};
    PhChiave *k = carica_chiave(&e);
    PhAdb *a = k != NULL ? collega(k, &e) : NULL;
    if (a == NULL) {
        if (k != NULL)
            ph_chiave_libera(k);
        return fallisci(&e, "collegamento fallito");
    }
    int uscita = 1;
    FILE *out = NULL;
    PhVideoFlusso *f = NULL;
    PhTelefonoInfo info;
    memset(&info, 0, sizeof info);
    PhComponente *c = ph_componente_avvia(a, jar, evento, NULL, &info, 60000, &e);
    if (c == NULL) {
        fallisci(&e, "componente non avviato");
        goto fine;
    }
    printf("telefono: %s, api %u, schermo %ux%u, %u dpi\n", info.modello, info.api,
           info.larghezza, info.altezza, info.densita);

    out = fopen(uscita_file, "wb");
    if (out == NULL) {
        fprintf(stderr, "errore: %s non si puo' creare\n", uscita_file);
        goto fine;
    }
    PhVideoRichiesta r = {.larghezza = (uint16_t)w, .altezza = (uint16_t)h,
                          .densita = (uint16_t)(dpi >= 0 ? dpi : info.densita),
                          .bit_al_secondo = 8000000, .fps = 30, .pacchetto = pacchetto};
    f = ph_video_apri(c, &r, &e);
    if (f == NULL) {
        fallisci(&e, "sessione video non aperta");
        goto fine;
    }

    uint16_t vw = (uint16_t)w, vh = (uint16_t)h; /* misure vere dall'ultimo pacchetto */
    double t0 = ora_s(), ultimo_stampa = t0;
    bool tocco_fatto = false;
    unsigned fotogrammi = 0, chiavi = 0;
    uint64_t byte = 0;
    uscita = 0;
    while (ora_s() - t0 < secondi) {
        PhVideoPacchetto p;
        int n = ph_video_leggi(f, &p, 200, &e);
        if (n < 0 && n != PH_CANALE_TEMPO) {
            fallisci(&e, "lettura video fallita");
            uscita = 1;
            break;
        }
        if (n == 0) {
            fprintf(stderr, "il flusso video e' finito\n");
            break;
        }
        if (n == 1) {
            if (fwrite(p.dati, 1, p.lunghezza, out) != p.lunghezza) {
                fprintf(stderr, "errore: scrittura su %s fallita\n", uscita_file);
                uscita = 1;
                break;
            }
            byte += p.lunghezza;
            if (!p.configurazione) {
                fotogrammi++;
                if (p.chiave)
                    chiavi++;
            }
            if (p.larghezza > 0 && p.altezza > 0) {
                vw = p.larghezza;
                vh = p.altezza;
            }
        }
        double adesso = ora_s();
        if (adesso - ultimo_stampa >= 1.0) {
            printf("%2.0f s: %u fotogrammi, %llu byte, %u chiave\n", adesso - t0, fotogrammi,
                   (unsigned long long)byte, chiavi);
            fflush(stdout);
            fotogrammi = chiavi = 0;
            byte = 0;
            ultimo_stampa = adesso;
        }
        if (tocco && !tocco_fatto && adesso - t0 >= secondi / 2.0) {
            tocco_fatto = true;
            if (ph_input_tocco(c, ph_video_id(f), vw, vh, PH_TOCCO_GIU, (uint16_t)tx, (uint16_t)ty, 0, &e) != 0 ||
                ph_input_tocco(c, ph_video_id(f), vw, vh, PH_TOCCO_SU, (uint16_t)tx, (uint16_t)ty, 0, &e) != 0) {
                fallisci(&e, "tocco non inviato");
                uscita = 1;
                break;
            }
            fprintf(stderr, "tocco inviato in %u,%u\n", tx, ty);
        }
    }
fine:
    if (out != NULL && fclose(out) != 0) {
        fprintf(stderr, "errore: chiusura di %s fallita\n", uscita_file);
        uscita = 1;
    }
    if (f != NULL)
        ph_video_chiudi(f, true);
    if (c != NULL)
        ph_componente_chiudi(c);
    ph_adb_chiudi(a);
    ph_chiave_libera(k);
    return uscita;
}

static void uso(void)
{
    fprintf(stderr,
            "uso: yasde-prova [--chiave FILE] [--telefono HOST:PORTA] COMANDO ...\n"
            "  cerca [secondi]\n  abbina HOST:PORTA CODICE\n  shell COMANDO...\n"
            "  invia LOCALE REMOTO\n  ricevi REMOTO LOCALE\n  elenca CARTELLA\n  info PERCORSO\n"
            "  video JAR PACCHETTO LARGHEZZAxALTEZZA SECONDI USCITA.h264 [--dpi N] [--tocco X,Y]\n");
}

int main(int argc, char **argv)
{
    int i = 1;
    while (i + 1 < argc && argv[i][0] == '-') {
        if (strcmp(argv[i], "--chiave") == 0)
            g_chiave = argv[i + 1];
        else if (strcmp(argv[i], "--telefono") == 0)
            g_telefono = argv[i + 1];
        else
            break;
        i += 2;
    }
    if (i >= argc) {
        uso();
        return 1;
    }
    const char *c = argv[i];
    int n = argc - i - 1;
    char **a = argv + i + 1;
    if (strcmp(c, "cerca") == 0)
        return cmd_cerca(n, a);
    if (strcmp(c, "abbina") == 0)
        return cmd_abbina(n, a);
    if (strcmp(c, "shell") == 0)
        return cmd_shell(n, a);
    if (strcmp(c, "invia") == 0)
        return cmd_copia(true, n, a);
    if (strcmp(c, "ricevi") == 0)
        return cmd_copia(false, n, a);
    if (strcmp(c, "elenca") == 0)
        return cmd_elenca(n, a);
    if (strcmp(c, "info") == 0)
        return cmd_info(n, a);
    if (strcmp(c, "video") == 0)
        return cmd_video(n, a);
    uso();
    return 1;
}
