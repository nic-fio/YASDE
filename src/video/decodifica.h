/* Decodifica H.264 dei flussi che arrivano dal telefono. La fa FFmpeg
 * (libavcodec), con l'accelerazione della scheda grafica. Scelta del
 * committente (23 nov 2026, "strada A"): VA-API per prima (il fotogramma
 * arriva alla shell senza copie), Vulkan dove il driver la offre di fabbrica
 * come ripiego (oggi il fotogramma viene copiato in memoria), il software
 * solo per ultimo.
 * Non sa nulla di finestre: consegna fotogrammi (AVFrame) a chi disegna. */
#ifndef PH_DECODIFICA_H
#define PH_DECODIFICA_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "comune/errore.h"

struct AVFrame;

typedef enum {
    PH_VIA_AUTOMATICA = 0, /* VA-API, poi Vulkan se di fabbrica, poi software */
    PH_VIA_VULKAN,
    PH_VIA_VAAPI,
    PH_VIA_SOFTWARE,
} PhVia;

typedef struct PhDecodifica PhDecodifica;

/* Con una via esplicita (non AUTOMATICA) fallisce se quella via non c'e':
 * nessun ripiego silenzioso, serve per misurare. */
PhDecodifica *ph_decodifica_crea(PhVia via, PhErrore *e);
void ph_decodifica_libera(PhDecodifica *d);
/* La sequenza dei tentativi della via AUTOMATICA, data la disponibilita' di
 * Vulkan di fabbrica: scrive in sequenza[] (3 posti) e torna quanti. Funzione
 * pura, separata per poterla provare senza scheda grafica. */
size_t ph_decodifica_ordine_automatico(bool vulkan_di_fabbrica, PhVia sequenza[3]);
/* La via davvero in uso (mai AUTOMATICA) e il suo nome per i log. Tiene conto
 * del ripiego: se la scheda ha aperto ma FFmpeg e' passato al software (il
 * formato accelerato non e' stato offerto), torna PH_VIA_SOFTWARE. Il ripiego
 * si scopre al primo pacchetto: va riletta dopo ph_decodifica_dai /
 * ph_decodifica_prendi, dallo stesso thread che li chiama. (Aggiunta del
 * 24 nov 2026 per l'avviso a schermo «decodifica col processore».) */
PhVia ph_decodifica_via(const PhDecodifica *d);
/* La regola di ph_decodifica_via, pura: la via scelta, se FFmpeg e' passato
 * al software o no. Separata per poterla provare senza scheda grafica. */
PhVia ph_via_reale(PhVia scelta, bool ripiego_software);
const char *ph_via_nome(PhVia via);

/* Da' al decodificatore un pacchetto del protocollo (docs/protocollo.md
 * §2.3): una CONFIGURAZIONE (configurazione = true) oppure un fotogramma
 * completo, in formato Annex B. 0 riuscito, -1 errore. Un pacchetto rovinato
 * non e' fatale: torna -1 e si puo' continuare dal prossimo fotogramma chiave. */
int ph_decodifica_dai(PhDecodifica *d, bool configurazione, const uint8_t *dati, size_t n,
                      uint64_t tempo_us, PhErrore *e);

/* Prende il prossimo fotogramma pronto: 1 = *f riempito, 0 = nessuno per
 * ora, -1 errore. Il fotogramma e' di chi chiama (av_frame_free). Il suo
 * campo pts e' il tempo_us del pacchetto. Con le vie accelerate i dati
 * restano nella memoria della scheda grafica (formato VAAPI o VULKAN). */
int ph_decodifica_prendi(PhDecodifica *d, struct AVFrame **f, PhErrore *e);

/* true se la scheda grafica predefinita offre di fabbrica la decodifica
 * H.264 con Vulkan (estensione presente senza forzature). */
bool ph_decodifica_vulkan_di_fabbrica(void);

#endif
