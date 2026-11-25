/* Finto telefono per i test del client ADB: un server TCP su localhost che
 * parla il protocollo ADB con TLS 1.3 (accetta una sola connessione) e offre
 * qualche servizio di prova. Serve solo ai test di questa cartella. */
#ifndef PH_ADB_FINTO_TELEFONO_H
#define PH_ADB_FINTO_TELEFONO_H

#include <stdbool.h>
#include <stdint.h>

#include "adb/adb.h"

typedef enum {
    FT_NORMALE,
    FT_CHIUDE_DOPO_TLS,  /* PC non abbinato: dopo il TLS il telefono chiude */
    FT_AUTH_DOPO_TLS,    /* PC non abbinato: dopo il TLS il telefono manda AUTH */
    FT_AUTH_SUBITO,      /* telefono senza TLS: risponde AUTH al primo CNXN */
    FT_BANNER_INGANNO,   /* come NORMALE, senza delayed_ack, ma con "features=delayed_ack" dentro il campo del modello */
} FtModo;

/* Massimo dato per pacchetto dichiarato dal finto telefono (piccolo, per
 * provare lo spezzettamento delle scritture). */
#define FT_MAX_DATI 65536u

/* Finestra di ricezione concessa dal finto telefono al client con delayed_ack. */
#define FT_FINESTRA 262144u

typedef struct FintoTelefono FintoTelefono;

/* Servizi: "eco:" rimanda indietro quello che riceve; "lento:" conferma ogni
 * WRTE dopo 100 ms; "silenzio:" non fa nulla; "chiudi:" manda "abc" e chiude;
 * "grande:" manda 300000 byte (ft_pattern); "rifiuta:" (e ogni sconosciuto)
 * rifiuta; "shell,v2,raw:<cmd>" con cmd = prova | muto | grosso | pendi | falso.
 * "sorgente:<N>" manda N byte (ft_pattern(3, posizione)) a pacchetti da
 * FT_MAX_DATI rispettando il controllo di flusso (il successivo solo dopo
 * l'OKAY del client); "sink:" conferma e conta i byte ricevuti; "nookay:"
 * non conferma mai; "tardivo:" risponde OKAY dopo 400 ms; "ostile:<n>" manda
 * pacchetti sbagliati: 0 = OKAY/WRTE/CLSE per id inesistenti (connessione
 * resta buona), 1 = magic errato, 2 = lunghezza oltre il massimo,
 * 3 = OKAY con carico di 2 byte, 4 = OKAY con valore oltre INT32_MAX (3 e 4 con delayed_ack).
 * "apertura0:" risponde all'apertura con un OKAY senza carico.
 * "fuori:<N>" manda N byte ignorando la finestra del client (violazione). */
FintoTelefono *ft_avvia(const PhChiave *chiave, FtModo modo, uint16_t *porta);
/* Come ft_avvia, con il massimo dato per pacchetto dichiarato nel CNXN. */
FintoTelefono *ft_avvia_max(const PhChiave *chiave, FtModo modo, uint32_t max_dati, uint16_t *porta);
/* Come ft_avvia_max; con delayed_ack = true il telefono dichiara la
 * caratteristica "delayed_ack" (e la usa se il CNXN del client la annuncia). */
FintoTelefono *ft_avvia_cfg(const PhChiave *chiave, FtModo modo, uint32_t max_dati, bool delayed_ack,
                            uint16_t *porta);
/* Chiude il socket di colpo (telefono spento / Wi-Fi caduto). */
void ft_cadi(FintoTelefono *ft);
/* Aspetta la fine del thread e libera tutto. */
void ft_ferma(FintoTelefono *ft);

/* Osservazioni (aggiornate dal thread del telefono). */
bool ft_certificato_client_ricevuto(FintoTelefono *ft);
int ft_violazioni_flusso(FintoTelefono *ft); /* WRTE arrivato prima dell'OKAY precedente, o troppo grande */
long ft_byte_lento(FintoTelefono *ft);       /* byte ricevuti sui canali "lento:" */
int ft_okay_ricevuti(FintoTelefono *ft);     /* OKAY mandati dal client */
int ft_wrte_inviati(FintoTelefono *ft);      /* WRTE mandati dal telefono (eco, shell, ...) */
int ft_clse_ricevuti(FintoTelefono *ft);     /* CLSE mandati dal client */
long ft_byte_sorgente(FintoTelefono *ft);    /* byte spediti dai canali "sorgente:" */
long ft_max_in_volo(FintoTelefono *ft);      /* massimo di byte del client non confermati (delayed_ack) */
long ft_byte_sink(FintoTelefono *ft);        /* byte ricevuti dai canali "sink:" */

/* Byte i-esimo di un flusso di prova riproducibile. */
uint8_t ft_pattern(uint32_t seme, size_t i);

#endif
