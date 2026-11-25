/* Prove di logica di shell-connessione, SENZA schermo: che cosa si mostra in
 * ogni stato, filtro del campo del codice, lettura di «indirizzo:porta»,
 * frasi d'errore, passaggi di fase. Valori finti. Controlla anche che il CSS
 * del pezzo si legga senza errori. */
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "logica_connessione.h"
#include "shell/prova_css.h"
#include "shell/shell.h"

void ph_risorse_connessione_register_resource(void);

static int errori;
#define CONTROLLA(cond)                                                                                              \
    do {                                                                                                             \
        if (!(cond)) {                                                                                               \
            fprintf(stderr, "prova_connessione.c:%d: fallito: %s\n", __LINE__, #cond);                               \
            errori++;                                                                                                \
        }                                                                                                            \
    } while (0)

static PhSrvCollegamento col(PhSrvStato stato, int noti)
{
    PhSrvCollegamento c;
    memset(&c, 0, sizeof c);
    c.stato = stato;
    c.telefoni_noti = noti;
    return c;
}

static void prova_contenuto(void)
{
    PhCnxContenuto k;

    /* CERCO, mai abbinato: tre passi, spinner, «Cerca di nuovo». */
    PhSrvCollegamento c = col(PH_SRV_CERCO, 0);
    CONTROLLA(ph_cnx_contenuto_per(&c, PH_CNX_FASE_RIPOSO, &k));
    CONTROLLA(k.vista == PH_CNX_VISTA_ISTRUZIONI);
    CONTROLLA(strcmp(k.titolo, "Collega il telefono") == 0);
    CONTROLLA(k.n_passi == 3 && strncmp(k.passi[0], "1.", 2) == 0 && strncmp(k.passi[2], "3.", 2) == 0);
    CONTROLLA(k.spinner && k.attesa != NULL && k.cerca_di_nuovo && !k.campo_codice && !k.mostra_motivo);
    CONTROLLA(k.comandi_attivi);

    /* CERCO, gia' abbinato: titolo diverso, motivo, riga dei piu' telefoni solo se arbitraria. */
    c = col(PH_SRV_CERCO, 1);
    CONTROLLA(ph_cnx_contenuto_per(&c, PH_CNX_FASE_RIPOSO, &k));
    CONTROLLA(k.vista == PH_CNX_VISTA_CERCO_NOTO && strcmp(k.titolo, "Cerco il telefono") == 0);
    CONTROLLA(k.mostra_motivo && k.testo != NULL && k.n_passi == 0 && !k.riga_arbitraria && k.spinner);
    c.scelta_arbitraria = true;
    CONTROLLA(ph_cnx_contenuto_per(&c, PH_CNX_FASE_RIPOSO, &k) && k.riga_arbitraria);
    /* ... ma non nella schermata delle istruzioni. */
    c = col(PH_SRV_CERCO, 0);
    c.scelta_arbitraria = true;
    CONTROLLA(ph_cnx_contenuto_per(&c, PH_CNX_FASE_RIPOSO, &k) && !k.riga_arbitraria);

    /* NON_ABBINATO: campo, testo del percorso, niente errore a riposo. */
    c = col(PH_SRV_NON_ABBINATO, 1);
    CONTROLLA(ph_cnx_contenuto_per(&c, PH_CNX_FASE_RIPOSO, &k));
    CONTROLLA(k.vista == PH_CNX_VISTA_ABBINA && strcmp(k.titolo, "Abbina il telefono") == 0);
    CONTROLLA(k.campo_codice && k.testo != NULL && strstr(k.testo, "Debug wireless") != NULL);
    CONTROLLA(!k.mostra_errore && k.attesa == NULL && !k.spinner && k.cerca_di_nuovo && k.comandi_attivi);

    /* In corso: attesa visibile, comandi inattivi. */
    CONTROLLA(ph_cnx_contenuto_per(&c, PH_CNX_FASE_IN_CORSO, &k));
    CONTROLLA(k.campo_codice && !k.comandi_attivi && k.spinner);
    CONTROLLA(k.attesa != NULL && strcmp(k.attesa, "Abbinamento in corso…") == 0);

    /* Errore: la riga c'e' e i comandi sono di nuovo attivi. */
    CONTROLLA(ph_cnx_contenuto_per(&c, PH_CNX_FASE_ERRORE, &k));
    CONTROLLA(k.mostra_errore && k.comandi_attivi && !k.spinner && k.campo_codice);

    /* Riuscito: «Abbinato: collego il telefono…», il campo sparisce (anche in CERCO). */
    CONTROLLA(ph_cnx_contenuto_per(&c, PH_CNX_FASE_RIUSCITO, &k));
    CONTROLLA(!k.campo_codice && k.spinner && strcmp(k.attesa, "Abbinato: collego il telefono…") == 0);
    PhSrvCollegamento cc = col(PH_SRV_CERCO, 1);
    CONTROLLA(ph_cnx_contenuto_per(&cc, PH_CNX_FASE_RIUSCITO, &k));
    CONTROLLA(!k.campo_codice && k.spinner && strcmp(k.titolo, "Abbina il telefono") == 0);
    CONTROLLA(ph_cnx_contenuto_per(&cc, PH_CNX_FASE_RIUSCITO_CERCATO, &k));
    CONTROLLA(!k.campo_codice && k.spinner && strcmp(k.attesa, "Abbinato: collego il telefono…") == 0);
    CONTROLLA(ph_cnx_contenuto_per(&c, PH_CNX_FASE_RIUSCITO_CERCATO, &k));
    CONTROLLA(!k.campo_codice && k.spinner);

    /* Riuscendo, il collegamento passa a CERCO mentre l'esito non e' ancora arrivato: la carta non cambia. */
    CONTROLLA(ph_cnx_contenuto_per(&cc, PH_CNX_FASE_IN_CORSO, &k));
    CONTROLLA(k.vista == PH_CNX_VISTA_ABBINA && k.campo_codice && !k.comandi_attivi && k.spinner);

    /* PERSO: titolo, motivo, pulsante. */
    c = col(PH_SRV_PERSO, 1);
    CONTROLLA(ph_cnx_contenuto_per(&c, PH_CNX_FASE_RIPOSO, &k));
    CONTROLLA(k.vista == PH_CNX_VISTA_PERSO && strcmp(k.titolo, "Connessione persa") == 0);
    CONTROLLA(k.mostra_motivo && k.cerca_di_nuovo && !k.spinner);

    /* Stati senza schermata. */
    c = col(PH_SRV_COLLEGATO, 1);
    CONTROLLA(!ph_cnx_contenuto_per(&c, PH_CNX_FASE_RIPOSO, &k));
    c = col(PH_SRV_BLOCCATO, 1);
    CONTROLLA(!ph_cnx_contenuto_per(&c, PH_CNX_FASE_RIPOSO, &k));
    c = col(PH_SRV_CHIUSO, 1);
    CONTROLLA(!ph_cnx_contenuto_per(&c, PH_CNX_FASE_RIPOSO, &k));
}

static void prova_fasi(void)
{
    /* Dopo l'esito: con il collegamento gia' in CERCO (come fa il collegamento,
     * che lo pubblica prima dell'esito) la fase ricorda di averlo visto. */
    CONTROLLA(ph_cnx_fase_dopo_esito(PH_SRV_ABB_RIUSCITO, PH_SRV_NON_ABBINATO) == PH_CNX_FASE_RIUSCITO);
    CONTROLLA(ph_cnx_fase_dopo_esito(PH_SRV_ABB_RIUSCITO, PH_SRV_CERCO) == PH_CNX_FASE_RIUSCITO_CERCATO);
    CONTROLLA(ph_cnx_fase_dopo_esito(PH_SRV_ABB_FALLITO, PH_SRV_NON_ABBINATO) == PH_CNX_FASE_ERRORE);
    CONTROLLA(ph_cnx_fase_dopo_esito(PH_SRV_ABB_RIFIUTATA, PH_SRV_NON_ABBINATO) == PH_CNX_FASE_ERRORE);
    CONTROLLA(ph_cnx_fase_dopo_esito(PH_SRV_ABB_SCADUTA, PH_SRV_NON_ABBINATO) == PH_CNX_FASE_ERRORE);
    CONTROLLA(ph_cnx_fase_dopo_esito(PH_SRV_ABB_NON_DISPONIBILE, PH_SRV_CERCO) == PH_CNX_FASE_ERRORE);
    CONTROLLA(ph_cnx_fase_riuscita(PH_CNX_FASE_RIUSCITO) && ph_cnx_fase_riuscita(PH_CNX_FASE_RIUSCITO_CERCATO));
    CONTROLLA(!ph_cnx_fase_riuscita(PH_CNX_FASE_RIPOSO) && !ph_cnx_fase_riuscita(PH_CNX_FASE_ERRORE));

    /* L'errore resta finche' si e' in NON_ABBINATO, poi cade. */
    CONTROLLA(ph_cnx_fase_con_stato(PH_CNX_FASE_ERRORE, PH_SRV_NON_ABBINATO) == PH_CNX_FASE_ERRORE);
    CONTROLLA(ph_cnx_fase_con_stato(PH_CNX_FASE_ERRORE, PH_SRV_CERCO) == PH_CNX_FASE_RIPOSO);
    /* «Riuscito» dura mentre il collegamento riparte, poi cade. */
    CONTROLLA(ph_cnx_fase_con_stato(PH_CNX_FASE_RIUSCITO, PH_SRV_CERCO) == PH_CNX_FASE_RIUSCITO_CERCATO);
    CONTROLLA(ph_cnx_fase_con_stato(PH_CNX_FASE_RIUSCITO, PH_SRV_COLLEGATO) == PH_CNX_FASE_RIPOSO);
    CONTROLLA(ph_cnx_fase_con_stato(PH_CNX_FASE_RIUSCITO, PH_SRV_PERSO) == PH_CNX_FASE_RIPOSO);
    CONTROLLA(ph_cnx_fase_con_stato(PH_CNX_FASE_RIUSCITO_CERCATO, PH_SRV_CERCO) == PH_CNX_FASE_RIUSCITO_CERCATO);
    CONTROLLA(ph_cnx_fase_con_stato(PH_CNX_FASE_RIUSCITO_CERCATO, PH_SRV_COLLEGATO) == PH_CNX_FASE_RIPOSO);
    /* Prima di CERCO, NON_ABBINATO e' ancora l'attesa. */
    CONTROLLA(ph_cnx_fase_con_stato(PH_CNX_FASE_RIUSCITO, PH_SRV_NON_ABBINATO) == PH_CNX_FASE_RIUSCITO);
    /* Nessun vicolo cieco: RIUSCITO -> CERCO -> NON_ABBINATO torna a riposo (campo vuoto e attivo). */
    PhCnxFase f = ph_cnx_fase_con_stato(PH_CNX_FASE_RIUSCITO, PH_SRV_CERCO);
    f = ph_cnx_fase_con_stato(f, PH_SRV_NON_ABBINATO);
    CONTROLLA(f == PH_CNX_FASE_RIPOSO);
    PhSrvCollegamento c = col(PH_SRV_NON_ABBINATO, 1);
    PhCnxContenuto k;
    CONTROLLA(ph_cnx_contenuto_per(&c, f, &k) && k.campo_codice && k.comandi_attivi && k.attesa == NULL);
    /* Lo stesso quando l'esito arriva con il collegamento gia' in CERCO. */
    f = ph_cnx_fase_con_stato(ph_cnx_fase_dopo_esito(PH_SRV_ABB_RIUSCITO, PH_SRV_CERCO), PH_SRV_NON_ABBINATO);
    CONTROLLA(f == PH_CNX_FASE_RIPOSO);
    /* Durante un tentativo gli eventi non lo interrompono. */
    CONTROLLA(ph_cnx_fase_con_stato(PH_CNX_FASE_IN_CORSO, PH_SRV_NON_ABBINATO) == PH_CNX_FASE_IN_CORSO);
    CONTROLLA(ph_cnx_fase_con_stato(PH_CNX_FASE_IN_CORSO, PH_SRV_CERCO) == PH_CNX_FASE_IN_CORSO);
    CONTROLLA(ph_cnx_fase_con_stato(PH_CNX_FASE_RIPOSO, PH_SRV_CERCO) == PH_CNX_FASE_RIPOSO);
}

static void prova_errori(void)
{
    char out[256];
    CONTROLLA(!ph_cnx_errore_per(PH_SRV_ABB_RIUSCITO, "x", "y", out, sizeof out) && out[0] == '\0');

    /* FALLITO: la frase dell'esito, poi il motivo, poi la frase fissa. */
    CONTROLLA(ph_cnx_errore_per(PH_SRV_ABB_FALLITO, "Codice sbagliato.", "Motivo", out, sizeof out));
    CONTROLLA(strcmp(out, "Codice sbagliato.") == 0);
    CONTROLLA(ph_cnx_errore_per(PH_SRV_ABB_FALLITO, NULL, "Motivo", out, sizeof out) && strcmp(out, "Motivo") == 0);
    CONTROLLA(ph_cnx_errore_per(PH_SRV_ABB_FALLITO, "", "", out, sizeof out) && strstr(out, "Controlla il codice") != NULL);
    /* SCADUTA: la frase, altrimenti quella fissa (mai il motivo vecchio). */
    CONTROLLA(ph_cnx_errore_per(PH_SRV_ABB_SCADUTA, "Nessuna risposta.", "Motivo", out, sizeof out) &&
              strcmp(out, "Nessuna risposta.") == 0);
    CONTROLLA(ph_cnx_errore_per(PH_SRV_ABB_SCADUTA, NULL, "Motivo", out, sizeof out) && strstr(out, "in tempo") != NULL);
    /* RIFIUTATA: sempre la stessa frase, qualunque cosa dica il servizio. */
    CONTROLLA(ph_cnx_errore_per(PH_SRV_ABB_RIFIUTATA, "Altro testo", "Motivo", out, sizeof out) &&
              strcmp(out, "Riprova tra un momento.") == 0);
    CONTROLLA(ph_cnx_errore_per(PH_SRV_ABB_NON_DISPONIBILE, NULL, NULL, out, sizeof out) &&
              strstr(out, "Cerca di nuovo") != NULL);
    /* Buffer piccolo: troncato e terminato. */
    char piccolo[8];
    CONTROLLA(ph_cnx_errore_per(PH_SRV_ABB_FALLITO, "Una frase molto lunga", NULL, piccolo, sizeof piccolo));
    CONTROLLA(strlen(piccolo) == sizeof piccolo - 1);
}

static void prova_codice(void)
{
    char out[PH_CNX_CIFRE + 1];
    CONTROLLA(ph_cnx_filtra_codice("123456", out, sizeof out) == 6 && strcmp(out, "123456") == 0);
    CONTROLLA(ph_cnx_filtra_codice("12 34-ab", out, sizeof out) == 4 && strcmp(out, "1234") == 0);
    CONTROLLA(ph_cnx_filtra_codice("1234567890", out, sizeof out) == 6 && strcmp(out, "123456") == 0);
    CONTROLLA(ph_cnx_filtra_codice("abc", out, sizeof out) == 0 && out[0] == '\0');
    CONTROLLA(ph_cnx_filtra_codice(NULL, out, sizeof out) == 0 && out[0] == '\0');
    CONTROLLA(ph_cnx_filtra_codice("\xd9\xa1\xd9\xa2", out, sizeof out) == 0); /* cifre non ASCII */
    char grande[32]; /* buffer largo: il limite di 6 cifre non deve dipendere dalla sua misura */
    CONTROLLA(ph_cnx_filtra_codice("1234567890", grande, sizeof grande) == 6 && strcmp(grande, "123456") == 0);
    char tre[4];
    CONTROLLA(ph_cnx_filtra_codice("123456", tre, sizeof tre) == 3 && strcmp(tre, "123") == 0);

    CONTROLLA(ph_cnx_abbina_ammesso("123456", PH_CNX_IND_VUOTO, false));
    CONTROLLA(ph_cnx_abbina_ammesso("123456", PH_CNX_IND_VALIDO, false));
    CONTROLLA(!ph_cnx_abbina_ammesso("12345", PH_CNX_IND_VUOTO, false));
    CONTROLLA(!ph_cnx_abbina_ammesso("1234567", PH_CNX_IND_VUOTO, false));
    CONTROLLA(!ph_cnx_abbina_ammesso("", PH_CNX_IND_VUOTO, false));
    CONTROLLA(!ph_cnx_abbina_ammesso("12345a", PH_CNX_IND_VUOTO, false));
    CONTROLLA(!ph_cnx_abbina_ammesso("123456", PH_CNX_IND_NON_VALIDO, false));
    CONTROLLA(!ph_cnx_abbina_ammesso("123456", PH_CNX_IND_VUOTO, true));

    char buf[8] = "abcdefg";
    ph_cnx_cancella(buf, sizeof buf);
    CONTROLLA(buf[0] == 0 && buf[6] == 0);
}

static void prova_indirizzo(void)
{
    char host[PH_CNX_HOST_MAX];
    int porta = 0;
    CONTROLLA(ph_cnx_leggi_indirizzo("", host, sizeof host, &porta) == PH_CNX_IND_VUOTO);
    CONTROLLA(ph_cnx_leggi_indirizzo("   ", host, sizeof host, &porta) == PH_CNX_IND_VUOTO);
    CONTROLLA(ph_cnx_leggi_indirizzo(NULL, host, sizeof host, &porta) == PH_CNX_IND_VUOTO);

    CONTROLLA(ph_cnx_leggi_indirizzo("192.0.2.20:37099", host, sizeof host, &porta) == PH_CNX_IND_VALIDO);
    CONTROLLA(strcmp(host, "192.0.2.20") == 0 && porta == 37099);
    CONTROLLA(ph_cnx_leggi_indirizzo("  192.0.2.20:1  ", host, sizeof host, &porta) == PH_CNX_IND_VALIDO && porta == 1);
    CONTROLLA(ph_cnx_leggi_indirizzo("192.0.2.20:65535", host, sizeof host, &porta) == PH_CNX_IND_VALIDO && porta == 65535);
    CONTROLLA(ph_cnx_leggi_indirizzo("[2001:db8::1]:5555", host, sizeof host, &porta) == PH_CNX_IND_VALIDO);
    CONTROLLA(strcmp(host, "2001:db8::1") == 0 && porta == 5555);
    /* Senza host e porta in uscita si puo' solo validare. */
    CONTROLLA(ph_cnx_leggi_indirizzo("192.0.2.20:5555", NULL, 0, NULL) == PH_CNX_IND_VALIDO);

    CONTROLLA(ph_cnx_leggi_indirizzo("192.0.2.20", host, sizeof host, &porta) == PH_CNX_IND_NON_VALIDO);      /* niente porta */
    CONTROLLA(ph_cnx_leggi_indirizzo("192.0.2.20:", host, sizeof host, &porta) == PH_CNX_IND_NON_VALIDO);
    CONTROLLA(ph_cnx_leggi_indirizzo("192.0.2.20:0", host, sizeof host, &porta) == PH_CNX_IND_NON_VALIDO);
    CONTROLLA(ph_cnx_leggi_indirizzo("192.0.2.20:65536", host, sizeof host, &porta) == PH_CNX_IND_NON_VALIDO);
    CONTROLLA(ph_cnx_leggi_indirizzo("192.0.2.20:123456", host, sizeof host, &porta) == PH_CNX_IND_NON_VALIDO);
    CONTROLLA(ph_cnx_leggi_indirizzo("192.0.2.20:12a", host, sizeof host, &porta) == PH_CNX_IND_NON_VALIDO);
    CONTROLLA(ph_cnx_leggi_indirizzo("192.0.2.20:-5", host, sizeof host, &porta) == PH_CNX_IND_NON_VALIDO);
    CONTROLLA(ph_cnx_leggi_indirizzo("192.0.2.300:5555", host, sizeof host, &porta) == PH_CNX_IND_NON_VALIDO);
    CONTROLLA(ph_cnx_leggi_indirizzo("esempio.invalid:5555", host, sizeof host, &porta) == PH_CNX_IND_NON_VALIDO);
    CONTROLLA(ph_cnx_leggi_indirizzo(":5555", host, sizeof host, &porta) == PH_CNX_IND_NON_VALIDO);
    CONTROLLA(ph_cnx_leggi_indirizzo("2001:db8::1:5555", host, sizeof host, &porta) == PH_CNX_IND_NON_VALIDO); /* IPv6 senza [] */
    CONTROLLA(ph_cnx_leggi_indirizzo("[192.0.2.20]:5555", host, sizeof host, &porta) == PH_CNX_IND_NON_VALIDO);
    CONTROLLA(ph_cnx_leggi_indirizzo("[2001:db8::1]5555", host, sizeof host, &porta) == PH_CNX_IND_NON_VALIDO);
    CONTROLLA(ph_cnx_leggi_indirizzo("[2001:db8::1:5555", host, sizeof host, &porta) == PH_CNX_IND_NON_VALIDO);
    char lungo[200];
    memset(lungo, '1', sizeof lungo - 1);
    lungo[sizeof lungo - 1] = '\0';
    CONTROLLA(ph_cnx_leggi_indirizzo(lungo, host, sizeof host, &porta) == PH_CNX_IND_NON_VALIDO);
    /* Buffer di uscita troppo piccolo: non valido, mai scritto oltre. */
    char corto[4];
    CONTROLLA(ph_cnx_leggi_indirizzo("192.0.2.20:5555", corto, sizeof corto, &porta) == PH_CNX_IND_NON_VALIDO);
}

/* Nessun colore scritto a mano nel CSS (la carta puo' essere su vetro chiaro). Rilevatore
 * comune (shell/prova_css.h). */
static void prova_css_senza_colori(void)
{
    int v = ph_prova_css_risorsa("/it/yasde/shell/connessione.css", "connessione.css", true);
    CONTROLLA(v == 0);
    CONTROLLA(ph_prova_css_autoprova() == 0);
}

int main(void)
{
    ph_risorse_connessione_register_resource();
    if (ph_shell_stile_controlla("/it/yasde/shell/connessione.css") != 0) {
        fprintf(stderr, "connessione.css: errori di sintassi\n");
        errori++;
    }
    prova_css_senza_colori();
    prova_contenuto();
    prova_fasi();
    prova_errori();
    prova_codice();
    prova_indirizzo();
    if (errori == 0)
        printf("prova_connessione: tutto bene\n");
    return errori == 0 ? 0 : 1;
}
