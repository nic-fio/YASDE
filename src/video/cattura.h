/* Cattura di una finestra: screenshot (PNG) e registrazione (MP4).
 * PROPOSTA di contratto. Vive in src/video/ perche' usa FFmpeg (libswscale e
 * il PNG per lo screenshot, libavformat per l'MP4), ma questo header non
 * espone nessun tipo di FFmpeg: chi lo include non deve conoscere FFmpeg.
 *
 * Errori: ogni funzione che puo' fallire riempie il PhErrore (puo' essere
 * NULL) con una frase in italiano; convenzione di src/comune/errore.h.
 * Quando una funzione torna errore, di norma non ha toccato nulla (nessun
 * file creato o lasciato a meta'); le eccezioni sono scritte funzione per
 * funzione. Nessuna funzione chiama exit() o abort() per un dato che arriva
 * dal telefono. Mai un file esistente viene sovrascritto.
 *
 * Contenitore delle registrazioni: MP4 FRAMMENTATO, scritto dal muxer
 * "mp4" di libavformat con empty_moov (moov con le sole descrizioni delle
 * tracce in testa), default_base_moof e frag_custom: i frammenti (coppie
 * moof+mdat) li chiudiamo noi, a ogni fotogramma chiave o circa ogni
 * secondo. Perche': i pacchetti H.264 non si ricodificano e il file e'
 * riproducibile anche se il processo muore a meta' (si perde al piu'
 * l'ultimo frammento, circa 1 secondo, ancora in memoria); un MP4 classico
 * senza moov finale e' illeggibile. SPS/PPS stanno nella descrizione della
 * traccia (avc1): i campioni non li contengono. Il muxer scrive su un nostro
 * AVIOContext che scrive sul descrittore aperto con O_EXCL. Tempi: video
 * 1/1000000 s, audio Opus 1/48000 s. */
#ifndef PH_CATTURA_H
#define PH_CATTURA_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

#include "comune/errore.h"

struct AVFrame; /* il tipo che esce da decodifica.h; mai incluso qui */

/* ---- Nomi dei file ---------------------------------------------------------
 * "<app ripulita> AAAA-MM-GG HH.MM.SS.<estensione>"; con indice >= 2 diventa
 * "<app ripulita> AAAA-MM-GG HH.MM.SS (indice).<estensione>". La ripulitura
 * toglie '/', '\\', ':', '*', '?', '"', '<', '>', '|' e i caratteri di
 * controllo (sostituiti da '_'), accorcia a 80 byte senza spezzare un
 * carattere UTF-8, toglie spazi e punti all'inizio e alla fine; se non resta
 * nulla usa "YASDE". L'ora e' quella locale di `quando`.
 * 0 riuscito, -1 errore (estensione vuota o con '/', buffer troppo piccolo,
 * ora non rappresentabile). `uscita` e' solo il nome, senza cartella. */
int ph_cattura_nome(const char *app, const char *estensione, time_t quando, unsigned indice,
                    char *uscita, size_t n, PhErrore *e);

/* ---- Screenshot ----------------------------------------------------------
 * Il fotogramma e' quello di ph_decodifica_prendi. Formati accettati: i
 * fotogrammi nella memoria della scheda grafica (VAAPI, Vulkan: vengono
 * portati in memoria normale con av_hwframe_transfer_data), poi qualunque
 * formato che libswscale sa leggere (YUV 8 e 10 bit, NV12, RGB...), con
 * larghezza e altezza da 1 a 16384. YUV -> RGB con la matrice dichiarata dal fotogramma (BT.601,
 * BT.709, BT.2020); se non dichiara nulla: BT.709 da 720 righe in su, BT.601
 * sotto; intervallo dichiarato (limitato se non dice nulla). Un formato che
 * swscale non legge e' un errore chiaro.
 * Le funzioni LEGGONO il fotogramma e non lo modificano; chi chiama ne resta
 * proprietario e non lo libera finche' la funzione non e' tornata. Con un
 * fotogramma nella memoria della scheda grafica il trasferimento puo' dover
 * essere serializzato con il decodificatore che lo ha prodotto: lo decide chi
 * chiama (non verificato su scheda reale). Tutte le funzioni sono utilizzabili
 * da qualunque thread in parallelo. */

/* PNG in memoria (per gli appunti: lo copia la shell). Alla riuscita *dati e'
 * allocato con malloc e va liberato con free(); a errore *dati = NULL. */
int ph_cattura_png_memoria(const struct AVFrame *f, uint8_t **dati, size_t *n, PhErrore *e);

/* PNG su file nuovo in `cartella` (se non esiste la crea, un solo livello),
 * con il nome di ph_cattura_nome e l'ora di adesso; se il nome esiste gia'
 * prova " (2)", " (3)"... fino a 999 (creazione atomica O_EXCL: nessun file
 * esistente viene toccato, nemmeno se due screenshot partono insieme).
 * `percorso` riceve il percorso completo scelto (cartella + '/' + nome).
 * A errore non resta nessun file (uno parziale viene cancellato). */
int ph_cattura_screenshot(const struct AVFrame *f, const char *cartella, const char *app,
                          char *percorso, size_t n_percorso, PhErrore *e);

/* ---- Registrazione -------------------------------------------------------
 * H.264 Annex B come arriva dal telefono (PhVideoPacchetto in
 * componente.h), scritto cosi' com'e' nell'MP4: nessuna ricodifica, solo i
 * pacchetti SPS/PPS/AUD tolti dai campioni (stanno nella descrizione della
 * traccia) e le lunghezze al posto dei codici di inizio. */
typedef struct PhRegistrazione PhRegistrazione;

typedef struct {
    bool iniziata;             /* visto il primo fotogramma chiave: il file ha le sue tracce */
    uint64_t video_accettati;  /* fotogrammi entrati nel file (l'ultimo esce a finisci) */
    uint64_t video_scartati;   /* prima del primo chiave, vuoti, senza dati H.264 validi */
    uint64_t tempi_corretti;   /* tempi non crescenti o con salto, corretti (vedi sotto) */
    uint64_t audio_accettati;  /* pacchetti Opus entrati nel file */
    uint64_t audio_scartati;   /* prima dell'inizio del video, vuoti, non Opus validi */
    uint64_t byte_scritti;     /* byte arrivati davvero nel file */
} PhRegistrazioneStato;

/* Crea il file "<cartella>/<app ripulita> data ora.mp4" (stessa regola di
 * nome e di " (2)" dello screenshot; cartella creata se manca) e lo tiene
 * aperto. `con_audio`: il file avra' anche una traccia Opus stereo 48 kHz
 * (va deciso qui: le tracce si dichiarano all'inizio). Il file e' vuoto
 * finche' non arriva il primo fotogramma chiave (servono le misure per
 * descrivere la traccia). `percorso` (puo' essere NULL) riceve il percorso
 * scelto. NULL a errore (nessun file lasciato). */
PhRegistrazione *ph_registrazione_inizia(const char *cartella, const char *app, bool con_audio,
                                         char *percorso, size_t n_percorso, PhErrore *e);
/* Se la scrittura dell'intestazione (al primo chiave) fallisce, la registrazione
 * e' guasta: finisci cancella il file e torna -1. */

/* Aggiunge un pacchetto video. Tutte le funzioni sono protette da un
 * lucchetto: due thread in parallelo si accodano. Il buffer `dati` e' solo letto e puo' essere
 * riusato appena la funzione torna.
 *  - configurazione = true: SPS/PPS (devono contenere almeno uno SPS valido e
 *    un PPS, altrimenti errore). Le misure del pacchetto sono ignorate.
 *    Uguale a quella gia' in uso: ignorata; diversa a registrazione
 *    iniziata: errore (il file non cambia). Lo stesso vale per SPS/PPS che
 *    arrivano DENTRO un fotogramma (anche chiave) a registrazione iniziata:
 *    diversi da quelli in uso -> errore e il fotogramma non entra; uguali ->
 *    il fotogramma entra e i parametri vengono tolti dal campione.
 *  - fotogramma: prima del primo fotogramma chiave (chiave = true E dati con un
 *    NAL IDR, e una configurazione gia' arrivata oppure dentro lo stesso
 *    pacchetto) i pacchetti si scartano. Dal primo chiave in poi le misure
 *    larghezza/altezza devono restare quelle del primo: se cambiano -> errore
 *    e il pacchetto non entra, la registrazione resta valida (chi chiama la
 *    finisce). Misure 0 o oltre 16383 sul primo chiave: errore.
 *  - lunghezza 0, dati senza codici di inizio, senza NAL video: scartato, non
 *    e' un errore. dati NULL con lunghezza > 0, oltre 32 MiB, tempo oltre
 *    2^50 us: errore.
 *  - tempi: il primo chiave e' il tempo 0 del file. Un tempo uguale o minore
 *    del precedente non entra com'e': il fotogramma precedente dura quanto
 *    quello prima di lui e la linea del tempo continua da li'. Un salto
 *    avanti e' preso per vero (lo schermo fermo manda pochi pacchetti) fino a
 *    30 minuti; oltre, il fotogramma precedente dura 30 minuti. tempi_corretti conta
 *    i casi. Lo scarto accumulato tra tempo grezzo e tempo nel file vale
 *    anche per l'audio, che resta allineato. Nessun fotogramma viene buttato
 *    per questo (butterebbe i successivi).
 *  - memoria finita: errore, il fotogramma non entra e nulla cambia (mai un
 *    campione vuoto nel file).
 * Torna 0 (accettato o scartato) o -1. Dopo un errore di SCRITTURA (disco
 * pieno, ecc.) il file viene riportato all'ultimo punto buono e chiuso, e
 * tutte le chiamate successive tornano -1; il file resta riproducibile. */
int ph_registrazione_aggiungi_video(PhRegistrazione *r, bool configurazione, bool chiave,
                                    uint16_t larghezza, uint16_t altezza, uint64_t tempo_us,
                                    const uint8_t *dati, size_t lunghezza, PhErrore *e);

/* Aggiunge un pacchetto Opus (campo `dati` di §2.5 di docs/protocollo.md).
 * Solo con con_audio = true (altrimenti -1). `tempo_us` deve stare sulla STESSA
 * linea del tempo dei pacchetti video: il campo "tempo" dell'audio del
 * telefono conta i campioni catturati, non e' confrontabile col video, e la
 * conversione la fa chi chiama (la shell). Prima del tempo del primo fotogramma
 * chiave i pacchetti si scartano. La durata di ogni pacchetto si legge dal suo
 * byte TOC (non dal tempo); il tempo serve solo a mettere il primo pacchetto
 * e a lasciare il silenzio di un buco (oltre 100 ms, al massimo 30 minuti); un tempo
 * che torna indietro si ignora e il pacchetto segue il precedente. Pacchetto
 * vuoto, oltre 65535 byte o con TOC non valido: scartato. Stesse regole di
 * errore di scrittura del video. Pre-skip dichiarato: 312 campioni (ritardo
 * del codificatore libopus; il telefono non lo trasmette). */
int ph_registrazione_aggiungi_audio(PhRegistrazione *r, uint64_t tempo_us, const uint8_t *dati,
                                    size_t lunghezza, PhErrore *e);

/* Copia i contatori. Sempre utilizzabile, anche dopo finisci. Con r = NULL
 * (o s = NULL) non cade: azzera *s (se c'e'). */
void ph_registrazione_stato(PhRegistrazione *r, PhRegistrazioneStato *s);

/* Scrive l'ultimo frammento e chiude il file. 0 = file completo; -1 = il file
 * puo' essere incompleto (errore di scrittura gia' avvenuto o ora) o gia'
 * chiusa. Se non e' mai arrivato un fotogramma chiave il file vuoto viene
 * cancellato e torna 0. Dopo di lei ogni altra chiamata torna -1.
 * Se un altro thread sta dentro una aggiungi, finisci aspetta che esca (e le
 * nuove tornano -1 senza fare nulla). Si chiama una volta sola. NON libera:
 * la memoria si libera con ph_registrazione_libera. */
int ph_registrazione_finisci(PhRegistrazione *r, PhErrore *e);

/* Libera tutto (se finisci non e' stata chiamata la chiama lei, ignorandone
 * l'esito). r = NULL e' permesso. Chi chiama deve garantire che nessun altro
 * thread usi r da qui in poi. */
void ph_registrazione_libera(PhRegistrazione *r);

#endif
