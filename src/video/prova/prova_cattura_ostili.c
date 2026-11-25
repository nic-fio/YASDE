/* Registrazione: pacchetti ostili, tempi, audio, disco pieno, limite di
 * dimensione del file, chiusura in parallelo. Niente FFmpeg: i file si
 * controllano con il lettore di box di aiuti.h. I NAL sono finti ma con la
 * forma giusta (SPS, PPS, IDR, non-IDR). */
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <time.h>

#include "aiuti.h"
#include "video/cattura_interno.h"

/* SPS e PPS veri, presi dal campione H.264 (il muxer li legge davvero); SPS2 e' uno SPS diverso. */
static uint8_t SPS[128], PPS[128], SPS2[128];
static size_t NSPS, NPPS;

static void carica_parametri(const char *file)
{
    size_t n;
    uint8_t *d = leggi_file(file, &n);
    for (size_t i = 0; i + 4 < n; i++) {
        if (!(d[i] == 0 && d[i + 1] == 0 && d[i + 2] == 1))
            continue;
        size_t j = i + 3;
        while (j + 3 <= n && !(d[j] == 0 && d[j + 1] == 0 && d[j + 2] == 1))
            j++;
        size_t fine = j + 3 <= n ? j : n;
        while (fine > i + 3 && d[fine - 1] == 0)
            fine--;
        size_t l = fine - (i + 3);
        int t = d[i + 3] & 0x1f;
        CONTROLLA(l + 4 <= sizeof SPS);
        if (t == 7 && NSPS == 0) {
            memcpy(SPS, "\0\0\0\1", 4);
            memcpy(SPS + 4, d + i + 3, l);
            NSPS = l + 4;
        } else if (t == 8 && NPPS == 0) {
            memcpy(PPS, "\0\0\0\1", 4);
            memcpy(PPS + 4, d + i + 3, l);
            NPPS = l + 4;
        }
        if (NSPS && NPPS)
            break;
    }
    CONTROLLA(NSPS > 0 && NPPS > 0);
    memcpy(SPS2, SPS, NSPS);
    SPS2[NSPS - 1] ^= 0x01; /* un bit diverso in fondo */
    free(d);
}

/* Fotogramma finto: [SPS PPS se in_banda] + NAL IDR/non-IDR con `n` byte di riempimento. */
static size_t fotogramma(uint8_t *out, bool idr, bool in_banda, size_t n)
{
    size_t k = 0;
    if (in_banda) {
        memcpy(out, SPS, NSPS);
        k += NSPS;
        memcpy(out + k, PPS, NPPS);
        k += NPPS;
    }
    static const uint8_t testa_idr[] = {0, 0, 0, 1, 0x65};
    static const uint8_t testa_p[] = {0, 0, 0, 1, 0x41};
    const uint8_t *t = idr ? testa_idr : testa_p;
    memcpy(out + k, t, 5);
    k += 5;
    memset(out + k, 0xAA, n); /* niente 00 00 01 dentro */
    return k + n;
}

static uint8_t buf[4096 + 256];

static PhRegistrazione *nuova(const char *dir, bool audio, char *percorso)
{
    PhErrore e = {0};
    PhRegistrazione *r = ph_registrazione_inizia(dir, "App Prova", audio, percorso, 600, &e);
    if (r == NULL)
        fprintf(stderr, "inizia: %s\n", e.testo);
    CONTROLLA(r != NULL);
    return r;
}

static int video(PhRegistrazione *r, bool chiave, uint16_t w, uint16_t h, uint64_t t, bool idr,
                 size_t riempimento)
{
    size_t n = fotogramma(buf, idr, false, riempimento);
    PhErrore e = {0};
    int rc = ph_registrazione_aggiungi_video(r, false, chiave, w, h, t, buf, n, &e);
    if (rc != 0 && getenv("PROVA_STAMPA"))
        fprintf(stderr, "video(): %s\n", e.testo);
    return rc;
}

static int config(PhRegistrazione *r, const uint8_t *sps, size_t ns, const uint8_t *pps, size_t np)
{
    uint8_t c[64];
    memcpy(c, sps, ns);
    memcpy(c + ns, pps, np);
    PhErrore e = {0};
    return ph_registrazione_aggiungi_video(r, true, false, 0, 0, 0, c, ns + np, &e);
}

static Riepilogo guarda(const char *percorso)
{
    Riepilogo r;
    leggi_mp4(percorso, &r);
    CONTROLLA(r.aperto);
    return r;
}

static void prova_base(const char *dir)
{
    char p[600];
    PhRegistrazione *r = nuova(dir, false, p);
    PhRegistrazioneStato s;
    CONTROLLA(config(r, SPS, NSPS, PPS, NPPS) == 0);
    /* Prima del chiave: scartati (non-IDR; flag chiave ma senza IDR; IDR ma senza flag chiave). */
    CONTROLLA(video(r, false, 320, 240, 1000, false, 20) == 0);
    CONTROLLA(video(r, true, 320, 240, 2000, false, 20) == 0);
    CONTROLLA(video(r, false, 320, 240, 3000, true, 20) == 0);
    ph_registrazione_stato(r, &s);
    CONTROLLA(!s.iniziata && s.video_scartati == 3 && s.video_accettati == 0 && s.byte_scritti == 0);
    /* Il chiave. */
    CONTROLLA(video(r, true, 320, 240, 10000, true, 100) == 0);
    for (int i = 1; i <= 130; i++)
        CONTROLLA(video(r, false, 320, 240, 10000 + (uint64_t)i * 16667, i % 60 == 0, 50) == 0);
    ph_registrazione_stato(r, &s);
    CONTROLLA(s.iniziata && s.video_accettati == 131 && s.tempi_corretti == 0);
    CONTROLLA(s.byte_scritti > 0); /* intestazione e primi frammenti gia' sul disco */
    PhErrore e = {0};
    CONTROLLA(ph_registrazione_finisci(r, &e) == 0);
    /* Dopo finisci tutto torna errore, finisci compresa. */
    CONTROLLA(video(r, false, 320, 240, 99999999, false, 5) == -1);
    CONTROLLA(ph_registrazione_aggiungi_audio(r, 0, buf, 5, &e) == -1);
    CONTROLLA(ph_registrazione_finisci(r, &e) == -1);
    ph_registrazione_stato(r, &s); /* ancora leggibile */
    CONTROLLA(s.video_accettati == 131);
    ph_registrazione_libera(r);
    Riepilogo ri = guarda(p);
    CONTROLLA(ri.box_completi && ri.n_flussi == 1);
    CONTROLLA(ri.campioni[0] == 131);
    CONTROLLA(ri.moof >= 3); /* frammenti: uno a ogni chiave */
    CONTROLLA(ri.sync[0] == 3); /* chiavi a 0, 60, 120 */
    CONTROLLA(ri.dur_min[0] == 16667 && ri.dur_max[0] == 16667);
    CONTROLLA(ri.pts_primo[0] == 0 && ri.pts_ultimo[0] == 130 * 16667); /* il primo chiave e' il tempo 0 */
    CONTROLLA(!ri.pts_non_crescenti);
    CONTROLLA(ri.nal_parametri == 0); /* i campioni non contengono SPS/PPS/AUD: stanno nell'avcC */
    /* Il campione non contiene SPS/PPS: dimensione = 4 + NAL. */
    size_t n;
    uint8_t *d = leggi_file(p, &n);
    CONTROLLA(memmem(d, n, SPS + 4, NSPS - 4) != NULL); /* lo SPS sta nell'avcC (nell'intestazione) */
    free(d);
}

static void prova_chiave_senza_configurazione(const char *dir)
{
    char p[600];
    PhRegistrazione *r = nuova(dir, false, p);
    PhRegistrazioneStato s;
    /* Nessuna configurazione: il chiave si scarta (il file non sarebbe leggibile). */
    CONTROLLA(video(r, true, 320, 240, 0, true, 50) == 0);
    ph_registrazione_stato(r, &s);
    CONTROLLA(!s.iniziata && s.video_scartati == 1);
    /* Configurazione dentro il chiave stesso: va bene. */
    size_t n = fotogramma(buf, true, true, 50);
    PhErrore e = {0};
    CONTROLLA(ph_registrazione_aggiungi_video(r, false, true, 320, 240, 16667, buf, n, &e) == 0);
    ph_registrazione_stato(r, &s);
    CONTROLLA(s.iniziata && s.video_accettati == 1);
    CONTROLLA(ph_registrazione_finisci(r, &e) == 0);
    ph_registrazione_libera(r);
    Riepilogo ri = guarda(p);
    CONTROLLA(ri.campioni[0] == 1 && ri.box_completi && ri.nal_parametri == 0);
}

static void prova_mai_iniziata(const char *dir)
{
    char p[600];
    PhRegistrazione *r = nuova(dir, false, p);
    CONTROLLA(access(p, F_OK) == 0); /* il nome e' gia' preso */
    CONTROLLA(video(r, false, 320, 240, 1, false, 20) == 0);
    PhErrore e = {0};
    CONTROLLA(ph_registrazione_finisci(r, &e) == 0);
    CONTROLLA(access(p, F_OK) != 0); /* vuoto: cancellato */
    ph_registrazione_libera(r);
    /* libera senza finisci: chiude da sola. */
    r = nuova(dir, false, p);
    ph_registrazione_libera(r);
    CONTROLLA(access(p, F_OK) != 0);
}

static void prova_pacchetti_ostili(const char *dir)
{
    char p[600];
    PhRegistrazione *r = nuova(dir, false, p);
    PhRegistrazioneStato s;
    PhErrore e = {0};
    CONTROLLA(config(r, SPS, NSPS, PPS, NPPS) == 0);
    /* Lunghezza zero e dati NULL con lunghezza zero: scartati, non errore. */
    CONTROLLA(ph_registrazione_aggiungi_video(r, false, true, 320, 240, 1, buf, 0, &e) == 0);
    CONTROLLA(ph_registrazione_aggiungi_video(r, false, true, 320, 240, 1, NULL, 0, &e) == 0);
    /* NULL con lunghezza: errore. */
    CONTROLLA(ph_registrazione_aggiungi_video(r, false, true, 320, 240, 1, NULL, 10, &e) == -1);
    CONTROLLA(e.testo[0] != 0);
    /* Byte senza codici di inizio: scartati. */
    memset(buf, 0x55, 100);
    CONTROLLA(ph_registrazione_aggiungi_video(r, false, true, 320, 240, 1, buf, 100, &e) == 0);
    /* Codice di inizio ma NAL vuoti / con bit proibito. */
    static const uint8_t brutti[] = {0, 0, 1, 0, 0, 1, 0x80, 0x01, 0, 0, 1};
    CONTROLLA(ph_registrazione_aggiungi_video(r, false, true, 320, 240, 1, brutti, sizeof brutti, &e) == 0);
    /* Lunghezza enorme: rifiutata prima di leggere. */
    uint8_t *grande = malloc((33u << 20));
    CONTROLLA(grande != NULL);
    CONTROLLA(ph_registrazione_aggiungi_video(r, false, true, 320, 240, 1, grande, 33u << 20, &e) == -1);
    free(grande);
    /* Tempo falso. */
    size_t n = fotogramma(buf, true, false, 30);
    CONTROLLA(ph_registrazione_aggiungi_video(r, false, true, 320, 240, UINT64_MAX, buf, n, &e) == -1);
    ph_registrazione_stato(r, &s);
    CONTROLLA(!s.iniziata && s.video_scartati == 4 && s.byte_scritti == 0);
    /* Misure 0 o enormi sul primo chiave: errore, e si puo' ripartire con misure buone. */
    CONTROLLA(video(r, true, 0, 240, 1, true, 30) == -1);
    CONTROLLA(video(r, true, 320, 0, 1, true, 30) == -1);
    CONTROLLA(video(r, true, 20000, 240, 1, true, 30) == -1);
    ph_registrazione_stato(r, &s);
    CONTROLLA(!s.iniziata);
    CONTROLLA(video(r, true, 320, 240, 5000, true, 30) == 0);
    /* Misure cambiate: errore, il pacchetto non entra, la registrazione prosegue. */
    CONTROLLA(video(r, false, 320, 240, 6000, false, 30) == 0);
    CONTROLLA(video(r, false, 640, 240, 7000, false, 30) == -1);
    CONTROLLA(video(r, true, 320, 480, 8000, true, 30) == -1);
    CONTROLLA(video(r, false, 320, 240, 9000, false, 30) == 0);
    ph_registrazione_stato(r, &s);
    CONTROLLA(s.video_accettati == 3);
    /* Configurazione: senza PPS o senza SPS errore; uguale ignorata; diversa errore. */
    CONTROLLA(ph_registrazione_aggiungi_video(r, true, false, 0, 0, 0, SPS, NSPS, &e) == -1);
    CONTROLLA(ph_registrazione_aggiungi_video(r, true, false, 0, 0, 0, PPS, NPPS, &e) == -1);
    CONTROLLA(ph_registrazione_aggiungi_video(r, true, false, 0, 0, 0, buf, 0, &e) == -1);
    CONTROLLA(config(r, SPS, NSPS, PPS, NPPS) == 0);
    CONTROLLA(config(r, SPS2, NSPS, PPS, NPPS) == -1);
    /* SPS/PPS dentro un fotogramma: diversi da quelli in uso -> errore e il fotogramma non entra;
     * uguali -> il fotogramma entra (i parametri non finiscono nel campione). */
    ph_registrazione_stato(r, &s);
    uint64_t prima = s.video_accettati;
    size_t nb = 0;
    memcpy(buf, SPS2, NSPS);
    nb += NSPS;
    memcpy(buf + nb, PPS, NPPS);
    nb += NPPS;
    nb += fotogramma(buf + nb, true, false, 40);
    CONTROLLA(ph_registrazione_aggiungi_video(r, false, true, 320, 240, 9500, buf, nb, &e) == -1);
    CONTROLLA(strstr(e.testo, "SPS/PPS") != NULL);
    ph_registrazione_stato(r, &s);
    CONTROLLA(s.video_accettati == prima);
    nb = fotogramma(buf, true, true, 40);
    CONTROLLA(ph_registrazione_aggiungi_video(r, false, true, 320, 240, 9600, buf, nb, &e) == 0);
    ph_registrazione_stato(r, &s);
    CONTROLLA(s.video_accettati == prima + 1);
    CONTROLLA(video(r, false, 320, 240, 10000, false, 30) == 0);
    CONTROLLA(ph_registrazione_finisci(r, &e) == 0);
    ph_registrazione_libera(r);
    Riepilogo ri = guarda(p);
    CONTROLLA(ri.campioni[0] == 5 && ri.box_completi && ri.nal_parametri == 0);
}

static void prova_tempi(const char *dir)
{
    char p[600];
    PhRegistrazione *r = nuova(dir, false, p);
    PhRegistrazioneStato s;
    PhErrore e = {0};
    CONTROLLA(config(r, SPS, NSPS, PPS, NPPS) == 0);
    /* uguale, indietro, salto di un'ora e mezza, poi normale. */
    const uint64_t t[] = {1000, 1000, 500, 600, 100000600ULL, 100000700ULL};
    for (size_t i = 0; i < sizeof t / sizeof t[0]; i++)
        CONTROLLA(video(r, i == 0, 320, 240, t[i], i == 0, 30) == 0);
    ph_registrazione_stato(r, &s);
    CONTROLLA(s.tempi_corretti == 2 && s.video_accettati == 6);
    CONTROLLA(ph_registrazione_finisci(r, &e) == 0);
    ph_registrazione_libera(r);
    Riepilogo ri = guarda(p);
    CONTROLLA(ri.campioni[0] == 6 && ri.box_completi && !ri.pts_non_crescenti);
    /* Un salto avanti di 100 s e' reale: resta com'e'. Solo uguale e indietro si correggono. */
    CONTROLLA(ri.dur_min[0] == 100 && ri.dur_max[0] == 100000000);
    CONTROLLA(ri.durata[0] == 33333 + 33333 + 100 + 100000000 + 100); /* l'ultimo dura quanto il precedente */
}

static void opus(uint8_t *out, size_t n, uint8_t toc)
{
    memset(out, 0x11, n);
    out[0] = toc;
}

static void prova_audio(const char *dir)
{
    char p[600];
    PhRegistrazione *r = nuova(dir, true, p);
    PhRegistrazioneStato s;
    PhErrore e = {0};
    uint8_t a[40];
    opus(a, 40, 0xFC); /* CELT 20 ms, un frame: 960 campioni */
    CONTROLLA(config(r, SPS, NSPS, PPS, NPPS) == 0);
    /* Audio prima dell'inizio del video: scartato. */
    CONTROLLA(ph_registrazione_aggiungi_audio(r, 0, a, 40, &e) == 0);
    CONTROLLA(video(r, true, 320, 240, 1000000, true, 30) == 0);
    /* Prima del tempo del primo chiave: scartato. */
    CONTROLLA(ph_registrazione_aggiungi_audio(r, 999999, a, 40, &e) == 0);
    ph_registrazione_stato(r, &s);
    CONTROLLA(s.audio_scartati == 2 && s.audio_accettati == 0);
    /* Non validi: vuoto, troppo lungo, TOC code 3 con 0 frame, code 3 con 60 frame da 20 ms. */
    CONTROLLA(ph_registrazione_aggiungi_audio(r, 1000000, a, 0, &e) == 0);
    uint8_t cattivo[2] = {0xFF, 0x00};
    CONTROLLA(ph_registrazione_aggiungi_audio(r, 1000000, cattivo, 2, &e) == 0);
    cattivo[1] = 60;
    CONTROLLA(ph_registrazione_aggiungi_audio(r, 1000000, cattivo, 2, &e) == 0);
    uint8_t *molto = malloc(70000);
    CONTROLLA(molto != NULL);
    opus(molto, 70000, 0xFC);
    CONTROLLA(ph_registrazione_aggiungi_audio(r, 1000000, molto, 70000, &e) == 0);
    free(molto);
    CONTROLLA(ph_registrazione_aggiungi_audio(r, 1000000, NULL, 5, &e) == -1);
    ph_registrazione_stato(r, &s);
    CONTROLLA(s.audio_scartati == 6 && s.audio_accettati == 0);
    /* 60 pacchetti continui (con un po' di jitter), poi un buco di 2 s, poi 5 pacchetti; un tempo
     * indietro viene ignorato. */
    for (int i = 0; i < 60; i++) {
        uint64_t t = 1000000 + (uint64_t)i * 20000 + (i % 2 ? 3000 : 0);
        CONTROLLA(ph_registrazione_aggiungi_audio(r, t, a, 40, &e) == 0);
    }
    CONTROLLA(ph_registrazione_aggiungi_audio(r, 1000000, a, 40, &e) == 0); /* indietro: segue */
    uint64_t dopo = 1000000 + 60 * 20000 + 2000000;
    for (int i = 0; i < 5; i++)
        CONTROLLA(ph_registrazione_aggiungi_audio(r, dopo + (uint64_t)i * 20000, a, 40, &e) == 0);
    CONTROLLA(video(r, false, 320, 240, 1016667, false, 30) == 0);
    CONTROLLA(ph_registrazione_finisci(r, &e) == 0);
    ph_registrazione_libera(r);
    Riepilogo ri = guarda(p);
    CONTROLLA(ri.box_completi && ri.n_flussi == 2 && ri.campioni[0] == 2 && ri.tb_den[1] == 48000);
    CONTROLLA(ri.campioni[1] == 66 && ri.sync[1] == 66);
    CONTROLLA(ri.dur_min[1] == 960); /* tra i pacchetti continui 960 tick; il buco e' l'unico salto maggiore */
    CONTROLLA(ri.dur_max[1] == 153600 - 60 * 960);
    CONTROLLA(ri.pts_primo[1] == 0);
    /* Dopo il buco i pacchetti ripartono dal tempo vero: 3,2 s da t0 = 153600 tick, +4 pacchetti. */
    CONTROLLA(ri.pts_ultimo[1] == 153600 + 4 * 960);
    /* Audio chiesto a una registrazione senza audio: errore. */
    r = nuova(dir, false, p);
    CONTROLLA(ph_registrazione_aggiungi_audio(r, 0, a, 40, &e) == -1);
    ph_registrazione_libera(r);
}

static void prova_disco_pieno(void)
{
    int fd = open("/dev/full", O_WRONLY);
    CONTROLLA(fd >= 0);
    PhErrore e = {0};
    PhRegistrazione *r = ph_registrazione_inizia_su_fd(fd, false, &e);
    CONTROLLA(r != NULL);
    CONTROLLA(config(r, SPS, NSPS, PPS, NPPS) == 0);
    /* L'intestazione non si riesce a scrivere: errore chiaro, nessun crash. */
    size_t n = fotogramma(buf, true, false, 30);
    CONTROLLA(ph_registrazione_aggiungi_video(r, false, true, 320, 240, 0, buf, n, &e) == -1);
    CONTROLLA(strstr(e.testo, "No space") != NULL);
    CONTROLLA(video(r, false, 320, 240, 100, false, 30) == -1);
    CONTROLLA(video(r, true, 320, 240, 200, true, 30) == -1);
    PhRegistrazioneStato s;
    ph_registrazione_stato(r, &s);
    CONTROLLA(!s.iniziata);
    CONTROLLA(ph_registrazione_finisci(r, &e) == -1);
    CONTROLLA(strstr(e.testo, "No space") != NULL);
    ph_registrazione_libera(r);
}

/* Nel figlio: il file non puo' superare 50 KB (due frammenti da ~20 KB ci stanno, il terzo no); la scrittura fallisce a meta'. */
static int figlio_limite(const char *dir, char *percorso_out)
{
    struct sigaction sa = {.sa_handler = SIG_IGN};
    sigaction(SIGXFSZ, &sa, NULL);
    char p[600];
    PhRegistrazione *r = nuova(dir, false, p);
    snprintf(percorso_out, 600, "%s", p);
    CONTROLLA(config(r, SPS, NSPS, PPS, NPPS) == 0);
    struct rlimit rl = {50000, 50000};
    CONTROLLA(setrlimit(RLIMIT_FSIZE, &rl) == 0);
    int primo_errore = -1;
    for (int i = 0; i < 400; i++) {
        if (video(r, i % 20 == 0, 320, 240, (uint64_t)i * 16667, i % 20 == 0, 900) != 0) {
            primo_errore = i;
            break;
        }
    }
    CONTROLLA(primo_errore > 20); /* qualcosa e' stato scritto prima */
    CONTROLLA(video(r, false, 320, 240, 99999999, false, 30) == -1); /* guasta per sempre */
    PhErrore e = {0};
    CONTROLLA(ph_registrazione_finisci(r, &e) == -1 && e.testo[0] != 0);
    ph_registrazione_libera(r);
    return 0;
}

static void prova_limite_dimensione(const char *dir)
{
    int canale[2];
    CONTROLLA(pipe(canale) == 0);
    pid_t pid = fork();
    CONTROLLA(pid >= 0);
    if (pid == 0) {
        char p[600] = "";
        close(canale[0]);
        figlio_limite(dir, p);
        CONTROLLA(write(canale[1], p, strlen(p) + 1) > 0);
        _exit(0);
    }
    close(canale[1]);
    char p[600];
    CONTROLLA(read(canale[0], p, sizeof p) > 0);
    close(canale[0]);
    int st;
    CONTROLLA(waitpid(pid, &st, 0) == pid && WIFEXITED(st) && WEXITSTATUS(st) == 0);
    /* Il file e' stato riportato all'ultimo punto buono: tutti i box completi e coerenti. */
    size_t n;
    uint8_t *d = leggi_file(p, &n);
    CONTROLLA(n <= 50000);
    free(d);
    Riepilogo ri = guarda(p);
    CONTROLLA(ri.moof >= 2 && ri.box_completi && ri.campioni[0] > 0);
}

/* Processo che muore a meta': il file resta leggibile fino all'ultimo frammento scritto. */
static void prova_morte(const char *dir)
{
    int canale[2];
    CONTROLLA(pipe(canale) == 0);
    pid_t pid = fork();
    CONTROLLA(pid >= 0);
    if (pid == 0) {
        close(canale[0]);
        char p[600];
        PhRegistrazione *r = nuova(dir, false, p);
        CONTROLLA(config(r, SPS, NSPS, PPS, NPPS) == 0);
        for (int i = 0; i < 100; i++)
            CONTROLLA(video(r, i % 60 == 0, 320, 240, (uint64_t)i * 16667, i % 60 == 0, 200) == 0);
        CONTROLLA(write(canale[1], p, strlen(p) + 1) > 0);
        _exit(0); /* senza finisci: niente scrittura finale */
    }
    close(canale[1]);
    char p[600];
    CONTROLLA(read(canale[0], p, sizeof p) > 0);
    close(canale[0]);
    int st;
    CONTROLLA(waitpid(pid, &st, 0) == pid && WIFEXITED(st) && WEXITSTATUS(st) == 0);
    Riepilogo ri = guarda(p);
    CONTROLLA(ri.box_completi && ri.aperto);
    /* Il frammento dei primi 60 fotogrammi e' uscito quando e' arrivato il secondo chiave. */
    CONTROLLA(ri.campioni[0] == 60);
}


static void prova_tempo_assurdo(const char *dir)
{
    char p[600];
    PhRegistrazione *r = nuova(dir, false, p);
    PhRegistrazioneStato s;
    PhErrore e = {0};
    CONTROLLA(config(r, SPS, NSPS, PPS, NPPS) == 0);
    CONTROLLA(video(r, true, 320, 240, 0, true, 30) == 0);
    CONTROLLA(video(r, false, 320, 240, 4000000000ULL, false, 30) == 0); /* oltre i 30 minuti */
    ph_registrazione_stato(r, &s);
    CONTROLLA(s.tempi_corretti == 1);
    CONTROLLA(ph_registrazione_finisci(r, &e) == 0);
    ph_registrazione_libera(r);
    Riepilogo ri = guarda(p);
    CONTROLLA(ri.campioni[0] == 2 && ri.dur_max[0] == 1800000000LL && ri.pts_ultimo[0] == 1800000000LL);
}

/* Registra video e audio con tempi grezzi dati; l'audio e' continuo (20 ms). `quando_audio_dopo`:
 * i pacchetti audio fino al tempo del fotogramma con il salto arrivano PRIMA di quel fotogramma
 * (come in un flusso vero: l'audio di quel buco e' gia' arrivato). */
static void prova_allineamento_salto_90s(const char *dir)
{
    char p[600];
    PhRegistrazione *r = nuova(dir, true, p);
    PhErrore e = {0};
    uint8_t a[40];
    opus(a, 40, 0xFC);
    CONTROLLA(config(r, SPS, NSPS, PPS, NPPS) == 0);
    const uint64_t t0 = 2000000, salto = 90000000;
    int k = 0;
    for (int i = 0; i < 10; i++) {
        CONTROLLA(video(r, i == 0, 320, 240, t0 + (uint64_t)i * 33333, i == 0, 30) == 0);
        for (; t0 + (uint64_t)k * 20000 <= t0 + (uint64_t)i * 33333; k++)
            CONTROLLA(ph_registrazione_aggiungi_audio(r, t0 + (uint64_t)k * 20000, a, 40, &e) == 0);
    }
    /* L'audio del buco arriva prima del fotogramma dopo il salto. */
    uint64_t t_dopo = t0 + 9 * 33333 + salto;
    for (; t0 + (uint64_t)k * 20000 <= t_dopo; k++)
        CONTROLLA(ph_registrazione_aggiungi_audio(r, t0 + (uint64_t)k * 20000, a, 40, &e) == 0);
    CONTROLLA(video(r, false, 320, 240, t_dopo, false, 30) == 0);
    CONTROLLA(video(r, false, 320, 240, t_dopo + 33333, false, 30) == 0);
    CONTROLLA(ph_registrazione_aggiungi_audio(r, t0 + (uint64_t)k * 20000, a, 40, &e) == 0);
    k++;
    PhRegistrazioneStato s;
    ph_registrazione_stato(r, &s);
    CONTROLLA(s.tempi_corretti == 0); /* 90 s sono un salto vero, non un valore assurdo */
    CONTROLLA(ph_registrazione_finisci(r, &e) == 0);
    ph_registrazione_libera(r);
    Riepilogo ri = guarda(p);
    CONTROLLA(ri.box_completi && ri.campioni[0] == 12 && ri.campioni[1] == (uint64_t)k);
    /* Video e audio nello stesso istante vero: tempo grezzo - t0. */
    CONTROLLA(ri.pts_ultimo[0] == (int64_t)(9 * 33333 + salto + 33333));
    CONTROLLA(ri.pts_ultimo[1] == (int64_t)(k - 1) * 960); /* audio senza buchi: pacchetto k-1 a (k-1)*20 ms */
    int64_t audio_us = ri.pts_ultimo[1] * 1000000 / 48000;
    CONTROLLA(llabs(audio_us - ri.pts_ultimo[0]) < 40000); /* ultimi due campioni a meno di 40 ms */
}

/* Un salto oltre il massimo viene accorciato e lo scarto deve valere anche per l'audio. */
static void prova_allineamento_scarto(const char *dir)
{
    char p[600];
    PhRegistrazione *r = nuova(dir, true, p);
    PhErrore e = {0};
    uint8_t a[40];
    opus(a, 40, 0xFC);
    CONTROLLA(config(r, SPS, NSPS, PPS, NPPS) == 0);
    const uint64_t t0 = 1000000, salto = 4000000000ULL;
    CONTROLLA(video(r, true, 320, 240, t0, true, 30) == 0);
    for (int k = 0; k < 5; k++)
        CONTROLLA(ph_registrazione_aggiungi_audio(r, t0 + (uint64_t)k * 20000, a, 40, &e) == 0);
    CONTROLLA(video(r, false, 320, 240, t0 + salto, false, 30) == 0); /* il file lo accorcia a 30 minuti */
    for (int k = 0; k < 10; k++)
        CONTROLLA(ph_registrazione_aggiungi_audio(r, t0 + salto + (uint64_t)k * 20000, a, 40, &e) == 0);
    CONTROLLA(ph_registrazione_finisci(r, &e) == 0);
    ph_registrazione_libera(r);
    Riepilogo ri = guarda(p);
    CONTROLLA(ri.box_completi && ri.campioni[0] == 2 && ri.campioni[1] == 15);
    CONTROLLA(ri.pts_ultimo[0] == 1800000000LL);
    /* L'audio dopo il salto comincia dove comincia il video dopo il salto e prosegue senza altri buchi. */
    CONTROLLA(ri.pts_ultimo[1] == 1800000000LL / 1000000 * 48000 + 9 * 960);
}

/* Errore di scrittura causato da un pacchetto AUDIO (chiude il frammento): file riportato
 * all'ultimo punto buono, poi sempre -1. */
static void prova_errore_da_audio(const char *dir)
{
    int canale[2];
    CONTROLLA(pipe(canale) == 0);
    pid_t pid = fork();
    CONTROLLA(pid >= 0);
    if (pid == 0) {
        close(canale[0]);
        struct sigaction sa = {.sa_handler = SIG_IGN};
        sigaction(SIGXFSZ, &sa, NULL);
        char p[600];
        PhRegistrazione *r = nuova(dir, true, p);
        CONTROLLA(config(r, SPS, NSPS, PPS, NPPS) == 0);
        CONTROLLA(video(r, true, 320, 240, 0, true, 30) == 0);
        PhRegistrazioneStato s;
        ph_registrazione_stato(r, &s);
        CONTROLLA(s.byte_scritti > 0); /* l'intestazione e' sul file */
        struct rlimit rl = {s.byte_scritti + 300, s.byte_scritti + 300};
        CONTROLLA(setrlimit(RLIMIT_FSIZE, &rl) == 0);
        uint8_t a[40];
        opus(a, 40, 0xFC);
        PhErrore e = {0};
        int rc = 0, quanti = 0;
        for (int k = 0; k < 200 && rc == 0; k++) {
            rc = ph_registrazione_aggiungi_audio(r, (uint64_t)k * 20000, a, 40, &e);
            quanti++;
        }
        CONTROLLA(rc == -1 && quanti == 50 && e.testo[0] != 0); /* il 50.mo chiude il frammento */
        CONTROLLA(ph_registrazione_aggiungi_audio(r, 2000000, a, 40, &e) == -1);
        CONTROLLA(video(r, false, 320, 240, 100000, false, 30) == -1);
        CONTROLLA(ph_registrazione_finisci(r, &e) == -1);
        ph_registrazione_libera(r);
        CONTROLLA(write(canale[1], p, strlen(p) + 1) > 0);
        _exit(0);
    }
    close(canale[1]);
    char p[600];
    CONTROLLA(read(canale[0], p, sizeof p) > 0);
    close(canale[0]);
    int st;
    CONTROLLA(waitpid(pid, &st, 0) == pid && WIFEXITED(st) && WEXITSTATUS(st) == 0);
    Riepilogo ri = guarda(p); /* solo l'intestazione: nessun pacchetto a meta' */
    CONTROLLA(ri.box_completi && ri.campioni[0] == 0 && ri.campioni[1] == 0 && ri.n_flussi == 2);
}

static void prova_stato_nullo(void)
{
    PhRegistrazioneStato s;
    memset(&s, 0xAB, sizeof s);
    ph_registrazione_stato(NULL, &s);
    CONTROLLA(!s.iniziata && s.video_accettati == 0 && s.byte_scritti == 0);
    ph_registrazione_stato(NULL, NULL); /* non deve cadere */
}

/* Chiusura in parallelo. */
typedef struct {
    PhRegistrazione *r;
    pthread_mutex_t m;
    pthread_cond_t c;
    int fatti;
    volatile int fine;
} Parallelo;

static void *scrittore(void *arg)
{
    Parallelo *pa = arg;
    uint8_t mio[300];
    for (int i = 0;; i++) {
        size_t n = fotogramma(mio, i % 30 == 0, false, 100);
        PhErrore e = {0};
        if (ph_registrazione_aggiungi_video(pa->r, false, i % 30 == 0, 320, 240, (uint64_t)i * 16667, mio,
                                            n, &e) != 0)
            break;
        pthread_mutex_lock(&pa->m);
        pa->fatti++;
        pthread_cond_broadcast(&pa->c);
        pthread_mutex_unlock(&pa->m);
    }
    pa->fine = 1;
    return NULL;
}

static void prova_chiusura_parallela(const char *dir)
{
    char p[600];
    Parallelo pa = {.m = PTHREAD_MUTEX_INITIALIZER, .c = PTHREAD_COND_INITIALIZER};
    pa.r = nuova(dir, false, p);
    CONTROLLA(config(pa.r, SPS, NSPS, PPS, NPPS) == 0);
    pthread_t th;
    CONTROLLA(pthread_create(&th, NULL, scrittore, &pa) == 0);
    struct timespec scad;
    clock_gettime(CLOCK_REALTIME, &scad);
    scad.tv_sec += 10;
    pthread_mutex_lock(&pa.m);
    while (pa.fatti < 100)
        CONTROLLA(pthread_cond_timedwait(&pa.c, &pa.m, &scad) == 0);
    pthread_mutex_unlock(&pa.m);
    PhErrore e = {0};
    CONTROLLA(ph_registrazione_finisci(pa.r, &e) == 0);
    CONTROLLA(pthread_join(th, NULL) == 0); /* lo scrittore esce al primo -1 */
    CONTROLLA(pa.fine == 1);
    PhRegistrazioneStato s;
    ph_registrazione_stato(pa.r, &s);
    ph_registrazione_libera(pa.r);
    Riepilogo ri = guarda(p);
    CONTROLLA(ri.box_completi && ri.campioni[0] == s.video_accettati);
    CONTROLLA(s.video_accettati >= 100);
}

int main(int argc, char **argv)
{
    CONTROLLA(argc == 2);
    carica_parametri(argv[1]);
    char dir[64];
    cartella_prova(dir, sizeof dir, "prova-ostili");
    prova_base(dir);
    prova_chiave_senza_configurazione(dir);
    prova_mai_iniziata(dir);
    prova_pacchetti_ostili(dir);
    prova_tempi(dir);
    prova_audio(dir);
    prova_tempo_assurdo(dir);
    prova_allineamento_salto_90s(dir);
    prova_allineamento_scarto(dir);
    prova_errore_da_audio(dir);
    prova_stato_nullo();
    prova_disco_pieno();
    prova_limite_dimensione(dir);
    prova_morte(dir);
    prova_chiusura_parallela(dir);
    togli_cartella(dir);
    printf("prova_cattura_ostili: ok\n");
    return 0;
}
