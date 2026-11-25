/* Finestra di prova dell'ondata 1 (non e' la shell): apre un'app del telefono
 * in una finestra spoglia e la fa usare col mouse. Serve a provare tutta la
 * catena (collegamento, componente, video, decodifica, input) e a misurare la
 * latenza clic -> immagine.
 *
 * Disegno: SDL3 con il suo motore Vulkan. Qui i fotogrammi vengono copiati
 * dalla scheda grafica alla memoria e ricaricati: e' la via piu' semplice,
 * NON quella finale. Come comporre le finestre senza copie e' la domanda
 * della Fase 0 (specifica 4.3, punto aperto A1) e si decide con le misure.
 *
 * Audio (facoltativo, --audio): dopo la prima immagine si apre il canale audio
 * e parte il riproduttore. --ritardo-audio S aspetta S secondi dopo la prima
 * immagine (prova della regola dei 5 s, specifica 3.3); --volume N e' il
 * volume iniziale sul PC (0-100, default 60).
 *
 * Uso: finestra-prova [--chiave FILE] [--telefono HOST:PORTA] [--via V]
 *                     [--dpi N] [--audio] [--ritardo-audio S] [--volume N]
 *                     JAR PACCHETTO LARGHEZZAxALTEZZA
 * Tasti: Esc = indietro; rotella = scorrimento; chiudere la finestra = fine.
 * Frecce e Tab vanno al telefono. Con --audio: F10 = muto, F11 / F12 = volume
 * -5 / +5 sul PC (questi tasti non vanno al telefono, il testo non si tocca). */
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <SDL3/SDL.h>
#include <libavutil/frame.h>
#include <libavutil/hwcontext.h>
#include <libavutil/pixfmt.h>

#include "adb/adb.h"
#include "audio/audio.h"
#include "componente/componente.h"
#include "scoperta/scoperta.h"
#include "video/decodifica.h"

typedef struct {
    PhVideoFlusso *flusso;
    PhDecodifica *decodifica;
    pthread_mutex_t m;
    AVFrame *ultimo;          /* il fotogramma piu' recente non ancora disegnato */
    int64_t ultimo_pts;       /* il suo tempo del telefono (us): il trasferimento dalla GPU non lo conserva */
    Uint32 evento_fotogramma;
    atomic_bool ferma;
    atomic_long ricevuti, scartati, errori;
    atomic_llong clic_ns;     /* istante dell'ultimo clic in attesa di risposta, 0 = nessuno */
    atomic_long ultima_latenza_us;
} Stato;

static long long adesso_ns(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (long long)t.tv_sec * 1000000000LL + t.tv_nsec;
}

/* Thread: legge i pacchetti, decodifica, porta il fotogramma in memoria e lo
 * consegna. Se chi disegna e' indietro, il fotogramma vecchio si scarta:
 * si degrada nel tempo, mai accumulando ritardo. */
static void *lavoro(void *arg)
{
    Stato *s = arg;
    PhErrore e;
    while (!atomic_load(&s->ferma)) {
        PhVideoPacchetto p;
        int r = ph_video_leggi(s->flusso, &p, 200, &e);
        if (r == PH_CANALE_TEMPO)
            continue;
        if (r != 1) {
            if (!atomic_load(&s->ferma))
                fprintf(stderr, "flusso video finito: %s\n", r == 0 ? "chiuso dal telefono" : e.testo);
            break;
        }
        if (ph_decodifica_dai(s->decodifica, p.configurazione, p.dati, p.lunghezza, p.tempo_us,
                              &e) != 0) {
            atomic_fetch_add(&s->errori, 1);
            continue;
        }
        AVFrame *f = NULL;
        while (ph_decodifica_prendi(s->decodifica, &f, &e) == 1) {
            AVFrame *in_memoria = f;
            int64_t pts = f->pts;
            if (f->hw_frames_ctx != NULL) {
                in_memoria = av_frame_alloc();
                if (in_memoria == NULL || av_hwframe_transfer_data(in_memoria, f, 0) < 0) {
                    av_frame_free(&in_memoria);
                    av_frame_free(&f);
                    atomic_fetch_add(&s->errori, 1);
                    continue;
                }
                av_frame_free(&f);
            }
            atomic_fetch_add(&s->ricevuti, 1);
            long long clic = atomic_exchange(&s->clic_ns, 0);
            if (clic != 0)
                atomic_store(&s->ultima_latenza_us, (long)((adesso_ns() - clic) / 1000));
            pthread_mutex_lock(&s->m);
            if (s->ultimo != NULL) {
                av_frame_free(&s->ultimo);
                atomic_fetch_add(&s->scartati, 1);
            }
            s->ultimo = in_memoria;
            s->ultimo_pts = pts;
            pthread_mutex_unlock(&s->m);
            SDL_Event ev;
            SDL_zero(ev);
            ev.type = s->evento_fotogramma;
            SDL_PushEvent(&ev);
        }
    }
    SDL_Event ev;
    SDL_zero(ev);
    ev.type = SDL_EVENT_QUIT;
    SDL_PushEvent(&ev);
    return NULL;
}

/* Chiamata dal thread dell'audio quando il flusso finisce o si rompe: qui si
 * puo' solo scrivere (le funzioni di PhAudio non vanno chiamate da qui). */
static void avviso_audio(void *ctx, int esito, const char *testo)
{
    (void)ctx;
    fprintf(stderr, "audio: %s (%s)\n", testo, esito == PH_AUDIO_FINITO ? "finito" : "errore");
}

static void evento(uint32_t id, uint8_t che_cosa, uint16_t a, uint16_t b, void *utente)
{
    (void)utente;
    static const char *nomi[] = {"?", "l'app si e' chiusa", "schermo protetto", "misure cambiate",
                                 "errore del codificatore"};
    fprintf(stderr, "evento dal telefono (sessione %u): %s (%u, %u)\n", (unsigned)id,
            che_cosa < 5 ? nomi[che_cosa] : "?", (unsigned)a, (unsigned)b);
}

static PhAdb *collega(const char *telefono, const PhChiave *chiave, PhErrore *e)
{
    char host[64];
    unsigned porta = 0;
    if (telefono != NULL) {
        const char *due = strrchr(telefono, ':');
        if (due == NULL || (size_t)(due - telefono) >= sizeof host || sscanf(due + 1, "%u", &porta) != 1) {
            ph_errore(e, "telefono non valido: serve HOST:PORTA");
            return NULL;
        }
        memcpy(host, telefono, (size_t)(due - telefono));
        host[due - telefono] = '\0';
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
            ph_errore(e, "nessun telefono trovato in rete (debug wireless acceso?)");
            return NULL;
        }
        snprintf(host, sizeof host, "%s", t[scelto].indirizzo);
        porta = t[scelto].porta;
    }
    bool non_abbinato = false;
    PhAdb *a = ph_adb_collega(host, (uint16_t)porta, chiave, 10000, &non_abbinato, e);
    if (a == NULL && non_abbinato)
        ph_errore(e, "questo PC non e' abbinato al telefono");
    return a;
}

int main(int argc, char **argv)
{
    const char *chiave_file = NULL, *telefono = NULL;
    PhVia via = PH_VIA_AUTOMATICA;
    unsigned dpi = 0;
    bool con_audio = false;
    double ritardo_audio = 0;
    int volume_iniziale = 60;
    int i = 1;
    for (; i < argc && strncmp(argv[i], "--", 2) == 0; i++) {
        if (strcmp(argv[i], "--audio") == 0) {
            con_audio = true;
            continue;
        }
        if (i + 1 >= argc)
            break;
        if (strcmp(argv[i], "--ritardo-audio") == 0) ritardo_audio = atof(argv[++i]);
        else if (strcmp(argv[i], "--volume") == 0) volume_iniziale = atoi(argv[++i]);
        else if (strcmp(argv[i], "--chiave") == 0) chiave_file = argv[++i];
        else if (strcmp(argv[i], "--telefono") == 0) telefono = argv[++i];
        else if (strcmp(argv[i], "--dpi") == 0) dpi = (unsigned)atoi(argv[++i]);
        else if (strcmp(argv[i], "--via") == 0) {
            const char *v = argv[++i];
            via = strcmp(v, "vulkan") == 0 ? PH_VIA_VULKAN : strcmp(v, "vaapi") == 0 ? PH_VIA_VAAPI
                : strcmp(v, "software") == 0 ? PH_VIA_SOFTWARE : PH_VIA_AUTOMATICA;
        } else break;
    }
    unsigned larghezza = 0, altezza = 0;
    if (argc - i != 3 || sscanf(argv[i + 2], "%ux%u", &larghezza, &altezza) != 2) {
        fprintf(stderr, "uso: %s [--chiave FILE] [--telefono HOST:PORTA] [--via V] [--dpi N] "
                        "[--audio] [--ritardo-audio S] [--volume N] JAR PACCHETTO LARGHEZZAxALTEZZA\n", argv[0]);
        return 2;
    }
    const char *jar = argv[i], *pacchetto = argv[i + 1];
    char chiave_predefinita[512];
    if (chiave_file == NULL) {
        const char *casa = getenv("HOME");
        snprintf(chiave_predefinita, sizeof chiave_predefinita, "%s/.config/YASDE/chiave.pem",
                 casa ? casa : ".");
        chiave_file = chiave_predefinita;
    }

    int esito = 1;
    PhErrore e = {0};
    Stato s;
    memset(&s, 0, sizeof s);
    pthread_mutex_init(&s.m, NULL);
    PhChiave *chiave = NULL;
    PhAdb *adb = NULL;
    PhComponente *comp = NULL;
    PhAudio *audio = NULL;
    SDL_Window *finestra = NULL;
    SDL_Renderer *disegno = NULL;
    SDL_Texture *immagine = NULL;
    pthread_t thread;
    bool thread_avviato = false;

    chiave = ph_chiave_carica_o_crea(chiave_file, &e);
    if (chiave == NULL) goto errore;
    adb = collega(telefono, chiave, &e);
    if (adb == NULL) goto errore;
    PhTelefonoInfo info;
    comp = ph_componente_avvia(adb, jar, evento, NULL, &info, 20000, &e);
    if (comp == NULL) goto errore;
    fprintf(stderr, "telefono: %s, Android API %u, schermo %ux%u a %u dpi\n", info.modello,
            (unsigned)info.api, (unsigned)info.larghezza, (unsigned)info.altezza,
            (unsigned)info.densita);

    s.decodifica = ph_decodifica_crea(via, &e);
    if (s.decodifica == NULL) goto errore;
    fprintf(stderr, "decodifica: via %s\n", ph_via_nome(ph_decodifica_via(s.decodifica)));

    PhVideoRichiesta r = {.larghezza = (uint16_t)larghezza, .altezza = (uint16_t)altezza,
                          .densita = (uint16_t)(dpi ? dpi : info.densita),
                          .bit_al_secondo = 8000000, .fps = 60, .pacchetto = pacchetto};
    long long t_apri = adesso_ns();
    s.flusso = ph_video_apri(comp, &r, &e);
    if (s.flusso == NULL) goto errore;
    uint32_t id = ph_video_id(s.flusso);

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        ph_errore(&e, "SDL: %s", SDL_GetError());
        goto errore;
    }
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "vulkan");
    if (!SDL_CreateWindowAndRenderer("YASDE - finestra di prova", (int)larghezza,
                                     (int)altezza, SDL_WINDOW_RESIZABLE, &finestra, &disegno)) {
        /* Senza motore Vulkan si prova quello predefinito, e lo si dice. */
        SDL_ResetHint(SDL_HINT_RENDER_DRIVER);
        if (!SDL_CreateWindowAndRenderer("YASDE - finestra di prova", (int)larghezza,
                                         (int)altezza, SDL_WINDOW_RESIZABLE, &finestra, &disegno)) {
            ph_errore(&e, "finestra: %s", SDL_GetError());
            goto errore;
        }
    }
    fprintf(stderr, "disegno: motore %s\n", SDL_GetRendererName(disegno));
    SDL_SetRenderVSync(disegno, 1);
    SDL_StartTextInput(finestra);
    s.evento_fotogramma = SDL_RegisterEvents(1);

    if (pthread_create(&thread, NULL, lavoro, &s) != 0) {
        ph_errore(&e, "impossibile avviare il thread del video");
        goto errore;
    }
    thread_avviato = true;

    int v_larghezza = 0, v_altezza = 0; /* misure del video nell'immagine corrente */
    enum AVPixelFormat formato = AV_PIX_FMT_NONE;
    SDL_FRect dove = {0, 0, 0, 0};
    bool premuto = false, primo = true, da_disegnare = false;
    long disegnati = 0;
    long long ultimo_rapporto = adesso_ns();
    long ricevuti_prima = 0;
    bool audio_tentato = !con_audio; /* l'audio si prova una volta sola */
    long long t_prima_immagine = 0;
    int64_t pts_disegnato = -1;      /* tempo del telefono dell'ultimo fotogramma disegnato */
    bool scarto_noto = false;
    int64_t scarto_base_us = 0;

    for (bool vai = true; vai;) {
        SDL_Event ev;
        if (!SDL_WaitEventTimeout(&ev, 250))
            SDL_zero(ev);
        do {
            float rx = 0, ry = 0;
            switch (ev.type) {
            case SDL_EVENT_QUIT:
                vai = false;
                break;
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
            case SDL_EVENT_MOUSE_BUTTON_UP:
            case SDL_EVENT_MOUSE_MOTION:
            case SDL_EVENT_MOUSE_WHEEL: {
                if (v_larghezza == 0 || dove.w <= 0)
                    break;
                float wx = ev.type == SDL_EVENT_MOUSE_WHEEL ? ev.wheel.mouse_x
                         : ev.type == SDL_EVENT_MOUSE_MOTION ? ev.motion.x : ev.button.x;
                float wy = ev.type == SDL_EVENT_MOUSE_WHEEL ? ev.wheel.mouse_y
                         : ev.type == SDL_EVENT_MOUSE_MOTION ? ev.motion.y : ev.button.y;
                SDL_RenderCoordinatesFromWindow(disegno, wx, wy, &rx, &ry);
                /* Dalla finestra al video: si toglie il margine e si riporta
                 * alla misura del video dichiarata dal telefono (specifica 4.10). */
                float fx = (rx - dove.x) * (float)v_larghezza / dove.w;
                float fy = (ry - dove.y) * (float)v_altezza / dove.h;
                bool dentro = fx >= 0 && fy >= 0 && fx < (float)v_larghezza && fy < (float)v_altezza;
                if (fx < 0) fx = 0;
                if (fy < 0) fy = 0;
                if (fx > (float)(v_larghezza - 1)) fx = (float)(v_larghezza - 1);
                if (fy > (float)(v_altezza - 1)) fy = (float)(v_altezza - 1);
                uint16_t x = (uint16_t)fx, y = (uint16_t)fy;
                uint16_t vw = (uint16_t)v_larghezza, vh = (uint16_t)v_altezza;
                if (ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN && ev.button.button == SDL_BUTTON_LEFT && dentro) {
                    premuto = true;
                    atomic_store(&s.clic_ns, adesso_ns());
                    ph_input_tocco(comp, id, vw, vh, PH_TOCCO_GIU, x, y, 1, NULL);
                } else if (ev.type == SDL_EVENT_MOUSE_BUTTON_UP && ev.button.button == SDL_BUTTON_LEFT && premuto) {
                    premuto = false;
                    ph_input_tocco(comp, id, vw, vh, PH_TOCCO_SU, x, y, 0, NULL);
                } else if (ev.type == SDL_EVENT_MOUSE_MOTION && premuto) {
                    ph_input_tocco(comp, id, vw, vh, PH_TOCCO_MUOVI, x, y, 1, NULL);
                } else if (ev.type == SDL_EVENT_MOUSE_WHEEL && dentro) {
                    ph_input_rotella(comp, id, vw, vh, x, y, (int16_t)(ev.wheel.x * 120.0f),
                                     (int16_t)(ev.wheel.y * 120.0f), NULL);
                }
                break;
            }
            case SDL_EVENT_KEY_DOWN:
                if (con_audio && (ev.key.key == SDLK_F10 || ev.key.key == SDLK_F11 ||
                                  ev.key.key == SDLK_F12)) {
                    /* Tasti dell'audio: guadagno sul PC, il telefono non li vede. */
                    if (audio != NULL) {
                        if (ev.key.key == SDLK_F10) {
                            if (!ev.key.repeat)
                                ph_audio_muto(audio, !ph_audio_muto_leggi(audio));
                        } else {
                            int v = ph_audio_volume_leggi(audio) + (ev.key.key == SDLK_F12 ? 5 : -5);
                            ph_audio_volume(audio, v);
                        }
                    }
                    break;
                }
                if (ev.key.key == SDLK_ESCAPE)
                    ph_input_indietro(comp, id, NULL);
                else if (ev.key.key == SDLK_RETURN) {
                    ph_input_tasto(comp, id, true, 66, 0, NULL);  /* KEYCODE_ENTER */
                    ph_input_tasto(comp, id, false, 66, 0, NULL);
                } else if (ev.key.key == SDLK_UP || ev.key.key == SDLK_DOWN ||
                           ev.key.key == SDLK_LEFT || ev.key.key == SDLK_RIGHT ||
                           ev.key.key == SDLK_TAB) {
                    /* KEYCODE_DPAD_UP/DOWN/LEFT/RIGHT, KEYCODE_TAB */
                    int codice = ev.key.key == SDLK_UP ? 19 : ev.key.key == SDLK_DOWN ? 20
                               : ev.key.key == SDLK_LEFT ? 21 : ev.key.key == SDLK_RIGHT ? 22 : 61;
                    ph_input_tasto(comp, id, true, (uint32_t)codice, 0, NULL);
                    ph_input_tasto(comp, id, false, (uint32_t)codice, 0, NULL);
                } else if (ev.key.key == SDLK_BACKSPACE) {
                    ph_input_tasto(comp, id, true, 67, 0, NULL);  /* KEYCODE_DEL */
                    ph_input_tasto(comp, id, false, 67, 0, NULL);
                }
                break;
            case SDL_EVENT_TEXT_INPUT:
                ph_input_testo(comp, id, ev.text.text, NULL);
                break;
            case SDL_EVENT_WINDOW_EXPOSED:
            case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
                da_disegnare = true;
                break;
            default:
                break;
            }
        } while (SDL_PollEvent(&ev));

        pthread_mutex_lock(&s.m);
        AVFrame *f = s.ultimo;
        int64_t f_pts = s.ultimo_pts;
        s.ultimo = NULL;
        pthread_mutex_unlock(&s.m);
        if (f != NULL) {
            if (immagine == NULL || f->width != v_larghezza || f->height != v_altezza ||
                f->format != formato) {
                if (immagine != NULL)
                    SDL_DestroyTexture(immagine);
                SDL_PixelFormat pf = f->format == AV_PIX_FMT_NV12 ? SDL_PIXELFORMAT_NV12
                                   : SDL_PIXELFORMAT_IYUV;
                immagine = SDL_CreateTexture(disegno, pf, SDL_TEXTUREACCESS_STREAMING, f->width,
                                             f->height);
                v_larghezza = f->width;
                v_altezza = f->height;
                formato = f->format;
                if (immagine == NULL || (f->format != AV_PIX_FMT_NV12 && f->format != AV_PIX_FMT_YUV420P &&
                                         f->format != AV_PIX_FMT_YUVJ420P)) {
                    ph_errore(&e, "formato del fotogramma non gestito dalla finestra di prova");
                    av_frame_free(&f);
                    goto errore;
                }
            }
            if (f->format == AV_PIX_FMT_NV12)
                SDL_UpdateNVTexture(immagine, NULL, f->data[0], f->linesize[0], f->data[1],
                                    f->linesize[1]);
            else
                SDL_UpdateYUVTexture(immagine, NULL, f->data[0], f->linesize[0], f->data[1],
                                     f->linesize[1], f->data[2], f->linesize[2]);
            pts_disegnato = f_pts == AV_NOPTS_VALUE ? -1 : f_pts;
            av_frame_free(&f);
            if (primo) {
                primo = false;
                t_prima_immagine = adesso_ns();
                fprintf(stderr, "prima immagine dopo %.0f ms dall'apertura\n",
                        (double)(adesso_ns() - t_apri) / 1e6);
            }
            da_disegnare = true;
        }
        if (da_disegnare && immagine != NULL) {
            int ow = 0, oh = 0;
            SDL_GetRenderOutputSize(disegno, &ow, &oh);
            /* Il video si adatta alla finestra conservando le proporzioni. */
            float scala = SDL_min((float)ow / (float)v_larghezza, (float)oh / (float)v_altezza);
            dove.w = (float)v_larghezza * scala;
            dove.h = (float)v_altezza * scala;
            dove.x = ((float)ow - dove.w) / 2.0f;
            dove.y = ((float)oh - dove.h) / 2.0f;
            SDL_SetRenderDrawColor(disegno, 0, 0, 0, 255);
            SDL_RenderClear(disegno);
            SDL_RenderTexture(disegno, immagine, NULL, &dove);
            SDL_RenderPresent(disegno);
            disegnati++;
            da_disegnare = false;
        }
        long long ora = adesso_ns();
        /* L'audio parte solo dopo la prima immagine (e dopo il ritardo chiesto):
         * l'ordine video -> audio conta (specifica 3.3). Se fallisce si va avanti
         * senza audio. */
        if (!audio_tentato && !primo && (double)(ora - t_prima_immagine) / 1e9 >= ritardo_audio) {
            audio_tentato = true;
            PhErrore ea = {0};
            PhCanale *canale = ph_componente_canale(comp, "audio", &ea);
            if (canale != NULL)
                audio = ph_audio_crea(canale, avviso_audio, NULL, &ea);
            if (audio != NULL && ph_audio_avvia(audio, &ea) != 0) {
                ph_audio_distruggi(audio);
                audio = NULL;
            }
            if (audio != NULL) {
                ph_audio_volume(audio, volume_iniziale);
                fprintf(stderr, "audio: avviato, volume %d (%.1f s dopo la prima immagine)\n",
                        ph_audio_volume_leggi(audio),
                        (double)(adesso_ns() - t_prima_immagine) / 1e9);
            } else {
                fprintf(stderr, "audio non disponibile, si continua senza: %s\n", ea.testo);
            }
        }
        if (ora - ultimo_rapporto >= 2000000000LL) {
            long ric = atomic_load(&s.ricevuti);
            fprintf(stderr, "ultimi 2 s: %ld fotogrammi; in tutto %ld ricevuti, %ld disegnati, "
                            "%ld scartati, %ld errori; ultimo clic->immagine %ld ms\n",
                    ric - ricevuti_prima, ric, disegnati, atomic_load(&s.scartati),
                    atomic_load(&s.errori), atomic_load(&s.ultima_latenza_us) / 1000);
            if (audio != NULL) {
                PhAudioStat st;
                ph_audio_statistiche(audio, &st);
                /* Tempo video e tempo audio hanno origini diverse: conta solo la
                 * variazione dello scarto rispetto al primo valore misurato. */
                int64_t ta = ph_audio_tempo_telefono_us(audio);
                char deriva[80];
                snprintf(deriva, sizeof deriva, "n.d. (tempo video %lld, audio %lld us)",
                         (long long)pts_disegnato, (long long)ta);
                if (ta >= 0 && pts_disegnato >= 0) {
                    int64_t scarto = pts_disegnato - ta;
                    if (!scarto_noto) {
                        scarto_noto = true;
                        scarto_base_us = scarto;
                    }
                    snprintf(deriva, sizeof deriva, "%+.1f ms", (double)(scarto - scarto_base_us) / 1000.0);
                }
                fprintf(stderr, "  audio: %llu ricevuti, %llu persi, %llu sottoalimentazioni, "
                                "margine %d ms (obiettivo %d ms), volume %d%s, deriva audio-video %s\n",
                        (unsigned long long)st.ricevuti, (unsigned long long)st.persi,
                        (unsigned long long)st.sottoalimentazioni, st.margine_ms, st.obiettivo_ms,
                        ph_audio_volume_leggi(audio), ph_audio_muto_leggi(audio) ? " (muto)" : "",
                        deriva);
            }
            ricevuti_prima = ric;
            ultimo_rapporto = ora;
        }
    }
    esito = 0;
    goto fine;

errore:
    fprintf(stderr, "errore: %s\n", e.testo);
fine:
    atomic_store(&s.ferma, true);
    /* Il canale audio e' del riproduttore: va chiuso prima del componente. */
    ph_audio_distruggi(audio);
    if (thread_avviato)
        pthread_join(thread, NULL);
    if (s.flusso != NULL)
        ph_video_chiudi(s.flusso, true);
    if (comp != NULL)
        ph_componente_chiudi(comp);
    if (adb != NULL)
        ph_adb_chiudi(adb);
    ph_chiave_libera(chiave);
    ph_decodifica_libera(s.decodifica);
    av_frame_free(&s.ultimo);
    if (immagine != NULL) SDL_DestroyTexture(immagine);
    if (disegno != NULL) SDL_DestroyRenderer(disegno);
    if (finestra != NULL) SDL_DestroyWindow(finestra);
    SDL_Quit();
    pthread_mutex_destroy(&s.m);
    return esito;
}
