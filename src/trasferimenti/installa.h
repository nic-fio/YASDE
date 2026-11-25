/* Installazione e disinstallazione di app Android sul telefono (specifica
 * 5.9.1: "Installazione di APK trascinato: confermata dall'utente ... con
 * spiegazioni dei rifiuti in parole semplici"; 5.4: "Disinstalla").
 * PROPOSTA DI CONTRATTO. Usa solo file/file.h (copia), adb/adb.h
 * (ph_shell_esegui) e config/lingua.h (ph_t).
 *
 * LA CONFERMA DELL'UTENTE NON E' COMPITO DI QUESTE FUNZIONI: la chiede la
 * shell prima di chiamarle. Allo stesso modo "solo app utente" per la
 * disinstallazione lo decide chi chiama con i dati che ha gia'; qui, se pm
 * rifiuta perche' e' un'app di sistema, il messaggio lo dice.
 *
 * Sono funzioni sincrone, senza oggetto e senza thread propri: si chiamano da
 * un thread di lavoro (mai da quello dell'interfaccia: la copia e pm possono
 * durare minuti). Piu' chiamate insieme da thread diversi sono lecite. La
 * PhAdb deve restare valida fino al ritorno (stessa regola di ph_file_*:
 * chi la chiude mentre una funzione la usa e' chi l'ha creata, e deve
 * aspettare il ritorno o far cadere la connessione, che fa tornare errore).
 * Nessun lock e' tenuto durante la copia o i comandi; ogni attesa ha un
 * limite (copia: i limiti di file.h; pm install 120 s; pm uninstall 30 s;
 * pulizia 5 s). Callback: solo quella di avanzamento, chiamata dal thread di
 * chi chiama, durante la copia; puo' impostare *annulla ma non deve chiamare
 * funzioni di questo file. Per la lingua vale che i messaggi sono sempre
 * tradotti (ph_t) e non contengono percorsi del PC ne' testi tecnici: il
 * dettaglio tecnico e' solo in *e.
 *
 * Che cosa viene toccato (regola "fallimenti a meta'"): sul telefono
 *  - il file temporaneo /data/local/tmp/yasde-apk-<8 cifre esadecimali
 *    casuali>.apk (nome nostro, nuovo a ogni chiamata, scelto con /dev/urandom)
 *    viene SEMPRE cancellato dopo che la copia e' stata tentata: a successo,
 *    rifiuto, errore, annullamento o caduta della connessione;
 *  - se la cancellazione non si puo' confermare (comando fallito o connessione
 *    caduta) risultato->temporaneo_rimasto e' true, risultato->temporaneo
 *    contiene il percorso (per i registri; l'utente non puo' raggiungere quella
 *    cartella) e il messaggio dice che il file verra' tolto alla prossima
 *    connessione. Mantiene la promessa ph_inst_pulisci, che chi gestisce la
 *    connessione chiama a ogni nuova connessione;
 *  - l'app e' installata solo se esito == PH_INST_RIUSCITO (con una eccezione:
 *    se pm non risponde entro il tempo, esito ERRORE e il messaggio dice che
 *    non si sa se sia stata installata).
 * Prima di copiare si controlla che il file sia davvero un APK; se non lo e'
 * non si tocca nulla (nessun comando, nessuna copia). */
#ifndef PH_INSTALLA_H
#define PH_INSTALLA_H

#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "adb/adb.h"
#include "comune/errore.h"
#include "file/file.h"

typedef enum {
    PH_INST_RIUSCITO = 0,
    PH_INST_RIFIUTATO,   /* pm ha detto di no: motivo e codice dicono perche' */
    PH_INST_ANNULLATO,   /* annullato da chi chiama (flag annulla) */
    PH_INST_ERRORE       /* non e' stato possibile (file, copia, connessione, risposta incomprensibile) */
} PhInstEsito;

typedef enum {
    PH_INST_M_NESSUNO = 0,
    PH_INST_M_GIA_INSTALLATA,
    PH_INST_M_FIRMA_DIVERSA,       /* UPDATE_INCOMPATIBLE: firmata con un'altra chiave */
    PH_INST_M_VERSIONE_VECCHIA,    /* VERSION_DOWNGRADE */
    PH_INST_M_SPAZIO,
    PH_INST_M_APK_NON_VALIDO,
    PH_INST_M_ANDROID_VECCHIO,     /* OLDER_SDK */
    PH_INST_M_ARCHITETTURA,        /* NO_MATCHING_ABIS */
    PH_INST_M_BLOCCATA,            /* dall'utente, dal telefono o dall'amministratore */
    PH_INST_M_VERIFICA,            /* controllo di sicurezza del telefono */
    PH_INST_M_SOLO_PROVA,          /* TEST_ONLY */
    PH_INST_M_ANNULLATA_SUL_TELEFONO,
    PH_INST_M_SISTEMA,             /* disinstallazione: app di sistema */
    PH_INST_M_NON_INSTALLATA,      /* disinstallazione: pacchetto sconosciuto */
    PH_INST_M_APP_TROPPO_VECCHIA,  /* DEPRECATED_SDK_VERSION */
    PH_INST_M_SPLIT_MANCANTE,      /* MISSING_SPLIT: app divisa in piu' file */
    PH_INST_M_ALTRO                /* codice sconosciuto: "codice" lo riporta */
} PhInstMotivo;

typedef enum { PH_INST_OP_INSTALLA = 0, PH_INST_OP_DISINSTALLA } PhInstOperazione;

typedef struct {
    PhInstEsito esito;
    PhInstMotivo motivo;
    char codice[96];        /* codice di pm (es. INSTALL_FAILED_ALREADY_EXISTS), "" se non c'e' */
    char messaggio[768];    /* frase in parole semplici, nella lingua attiva (ph_t), sempre presente */
    bool temporaneo_rimasto;
    char temporaneo[96];    /* percorso sul telefono, solo se temporaneo_rimasto */
} PhInstRisultato;

typedef PhFileProgresso PhInstProgresso;

/* Toglie dal telefono i file temporanei rimasti: esegue
 * "rm -f /data/local/tmp/yasde-apk-*.apk"
 * (riga fissa, nessun dato esterno).
 * CHI GESTISCE LA CONNESSIONE LA CHIAMA A OGNI NUOVA CONNESSIONE, prima di
 * qualunque installazione, e mai mentre un ph_inst_installa e' in corso sullo
 * stesso telefono (ne cancellerebbe il temporaneo). Il prefisso
 * "yasde-apk-" con fine ".apk" non puo' coincidere con i file del
 * componente ("yasde-<8 cifre>.jar", "yasde-custode-*"): il nome
 * differisce gia' nel prefisso e la fine ".apk" esclude i jar. 0 se il
 * comando e' riuscito, -1 con *e altrimenti. Attesa massima 5 s. */
int ph_inst_pulisci(PhAdb *a, PhErrore *e);

/* 1 = e' un APK, 0 = non lo e', -1 = il file non si puo' leggere (errore in
 * *e). E' un APK se il nome finisce per ".apk" (senza badare alle maiuscole) E
 * il contenuto e' uno zip la cui directory centrale ha la voce
 * "AndroidManifest.xml" (nome esatto). Si legge solo la coda e la directory
 * centrale (al massimo 32 MiB); gli zip64 (oltre 4 GiB) risultano "non APK".
 * Ogni lunghezza letta dal file e' controllata contro i limiti del file. */
int ph_inst_riconosci(const char *percorso, PhErrore *e);

/* Installa l'APK del PC (gia' confermato dall'utente): controllo APK, copia con
 * ph_file_invia (progresso e annulla facoltativi; l'annullamento vale durante
 * la copia e subito prima di pm), "pm install -r", pulizia del temporaneo.
 * Compila sempre *risultato (anche quando torna ERRORE/ANNULLATO) e torna
 * risultato->esito. Se argomenti mancanti torna ERRORE e, se risultato non e'
 * NULL, lo compila. *e riceve il dettaglio tecnico. */
PhInstEsito ph_inst_installa(PhAdb *a, const char *apk_locale, PhInstProgresso progresso, void *utente,
                             const atomic_bool *annulla, PhInstRisultato *risultato, PhErrore *e);

/* Nome di pacchetto accettabile: da 1 a 255 caratteri tra [A-Za-z0-9_.], il
 * primo una lettera. E' l'unico controllo che lascia passare un nome in una
 * riga di comando. */
bool ph_inst_nome_pacchetto_valido(const char *nome);

/* "pm uninstall <pacchetto>". Toglie anche i dati dell'app: la CONFERMA che la
 * shell mostra prima di chiamare deve dirlo ("si perdono anche i dati
 * dell'app"). La conferma e' di chi chiama. Nome non valido: esito ERRORE e nessun comando eseguito. Torna
 * risultato->esito. */
PhInstEsito ph_inst_disinstalla(PhAdb *a, const char *pacchetto, PhInstRisultato *risultato, PhErrore *e);

/* Funzione pura: traduce l'uscita di pm in *risultato (esito, motivo, codice,
 * messaggio; i campi del temporaneo non vengono toccati). I buffer NON devono
 * essere terminati da '\0' (si usa solo la lunghezza) e possono contenere
 * qualunque byte; possono essere NULL con lunghezza 0. "Success" deve essere
 * una riga intera; il codice si cerca dopo "Failure [" o come parola
 * INSTALL_.../DELETE_...; senza ne' l'uno ne' l'altro l'esito e' ERRORE
 * (risposta non riconosciuta), mai RIUSCITO per caso. Il codice di uscita di
 * pm non conta: pm esce con 0 anche quando rifiuta. */
void ph_inst_analizza(PhInstOperazione op, const char *uscita, size_t n_uscita, const char *errori,
                      size_t n_errori, PhInstRisultato *risultato);

#endif
