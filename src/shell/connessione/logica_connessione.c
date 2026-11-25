/* Logica pura della schermata di prima connessione: vedi logica_connessione.h. */
#include "logica_connessione.h"

#include <glib.h>
#include <string.h>

#include "config/lingua.h"

bool ph_cnx_contenuto_per(const PhSrvCollegamento *c, PhCnxFase fase, PhCnxContenuto *k)
{
    memset(k, 0, sizeof *k);
    k->comandi_attivi = fase != PH_CNX_FASE_IN_CORSO;
    /* Riuscendo, il collegamento pubblica CERCO prima dell'esito: durante il
     * tentativo la carta resta quella dell'abbinamento (niente lampo di un'altra). */
    PhSrvStato stato = c->stato;
    if (stato == PH_SRV_CERCO && fase == PH_CNX_FASE_IN_CORSO)
        stato = PH_SRV_NON_ABBINATO;
    switch (stato) {
    case PH_SRV_CERCO:
        if (ph_cnx_fase_riuscita(fase)) {
            /* La carta resta quella dell'abbinamento: sparisce il campo e
             * resta la riga di attesa (poi l'ossatura la toglie). */
            k->vista = PH_CNX_VISTA_ABBINA;
            k->titolo = ph_t("Abbina il telefono");
            k->attesa = ph_t("Abbinato: collego il telefono…");
            k->spinner = true;
            k->cerca_di_nuovo = true;
            return true;
        }
        if (c->telefoni_noti <= 0) {
            k->vista = PH_CNX_VISTA_ISTRUZIONI;
            k->titolo = ph_t("Collega il telefono");
            k->passi[0] = ph_t("1. Sul telefono apri Impostazioni › Opzioni sviluppatore e attiva Debug wireless.");
            k->passi[1] = ph_t("2. Telefono e PC devono essere sulla stessa rete Wi-Fi.");
            k->passi[2] = ph_t("3. Quando il telefono compare qui ti chiederà il codice di abbinamento.");
            k->n_passi = 3;
            k->attesa = ph_t("Cerco telefoni in rete…");
            k->spinner = true;
            k->cerca_di_nuovo = true;
            return true;
        }
        k->vista = PH_CNX_VISTA_CERCO_NOTO;
        k->titolo = ph_t("Cerco il telefono");
        k->mostra_motivo = true;
        k->testo = ph_t("Controlla che il telefono sia acceso, sulla stessa rete Wi-Fi e con Debug wireless attivo.");
        k->riga_arbitraria = c->scelta_arbitraria;
        k->attesa = ph_t("Cerco in rete…");
        k->spinner = true;
        k->cerca_di_nuovo = true;
        return true;
    case PH_SRV_NON_ABBINATO:
        k->vista = PH_CNX_VISTA_ABBINA;
        k->titolo = ph_t("Abbina il telefono");
        k->campo_codice = !ph_cnx_fase_riuscita(fase);
        k->testo = ph_t("Sul telefono: Opzioni sviluppatore › Debug wireless › Associa dispositivo con codice. "
                        "Scrivi qui il codice a sei cifre che compare.");
        k->cerca_di_nuovo = true;
        if (fase == PH_CNX_FASE_IN_CORSO) {
            k->attesa = ph_t("Abbinamento in corso…");
            k->spinner = true;
        } else if (ph_cnx_fase_riuscita(fase)) {
            k->attesa = ph_t("Abbinato: collego il telefono…");
            k->spinner = true;
        } else if (fase == PH_CNX_FASE_ERRORE) {
            k->mostra_errore = true;
        }
        return true;
    case PH_SRV_PERSO:
        k->vista = PH_CNX_VISTA_PERSO;
        k->titolo = ph_t("Connessione persa");
        k->mostra_motivo = true;
        k->cerca_di_nuovo = true;
        return true;
    case PH_SRV_COLLEGATO:
    case PH_SRV_BLOCCATO:
    case PH_SRV_CHIUSO:
        break;
    }
    return false;
}

bool ph_cnx_fase_riuscita(PhCnxFase fase)
{
    return fase == PH_CNX_FASE_RIUSCITO || fase == PH_CNX_FASE_RIUSCITO_CERCATO;
}

PhCnxFase ph_cnx_fase_con_stato(PhCnxFase fase, PhSrvStato stato)
{
    switch (fase) {
    case PH_CNX_FASE_RIUSCITO:
        /* Dura finche' il collegamento sta ripartendo: CERCO lo conferma. */
        if (stato == PH_SRV_CERCO)
            return PH_CNX_FASE_RIUSCITO_CERCATO;
        return stato == PH_SRV_NON_ABBINATO ? fase : PH_CNX_FASE_RIPOSO;
    case PH_CNX_FASE_RIUSCITO_CERCATO:
        /* Gia' passato per CERCO: un nuovo NON_ABBINATO non e' piu' l'attesa
         * dell'abbinamento (il telefono non ci riconosce di nuovo). */
        return stato == PH_SRV_CERCO ? fase : PH_CNX_FASE_RIPOSO;
    case PH_CNX_FASE_ERRORE:
        return stato == PH_SRV_NON_ABBINATO ? fase : PH_CNX_FASE_RIPOSO;
    case PH_CNX_FASE_IN_CORSO:
    case PH_CNX_FASE_RIPOSO:
        break;
    }
    return fase;
}

PhCnxFase ph_cnx_fase_dopo_esito(PhSrvAbbEsito esito, PhSrvStato stato)
{
    if (esito != PH_SRV_ABB_RIUSCITO)
        return PH_CNX_FASE_ERRORE;
    /* Riuscendo, il collegamento di solito ha gia' pubblicato CERCO (prima
     * dell'esito): da qui in poi un NON_ABBINATO e' un fatto nuovo. */
    return stato == PH_SRV_CERCO ? PH_CNX_FASE_RIUSCITO_CERCATO : PH_CNX_FASE_RIUSCITO;
}

bool ph_cnx_errore_per(PhSrvAbbEsito esito, const char *frase, const char *motivo, char *out, size_t cap)
{
    const char *scelta = NULL;
    switch (esito) {
    case PH_SRV_ABB_RIUSCITO:
        if (cap > 0)
            out[0] = '\0';
        return false;
    case PH_SRV_ABB_FALLITO:
        if (frase != NULL && frase[0] != '\0')
            scelta = frase;
        else if (motivo != NULL && motivo[0] != '\0')
            scelta = motivo;
        else
            scelta = ph_t("Abbinamento non riuscito. Controlla il codice e riprova.");
        break;
    case PH_SRV_ABB_SCADUTA:
        if (frase != NULL && frase[0] != '\0')
            scelta = frase;
        else
            scelta = ph_t("Il telefono non ha risposto in tempo. Riprova.");
        break;
    case PH_SRV_ABB_RIFIUTATA:
        scelta = ph_t("Riprova tra un momento.");
        break;
    case PH_SRV_ABB_NON_DISPONIBILE:
        scelta = ph_t("Questa versione non sa abbinare il telefono. Prova con «Cerca di nuovo».");
        break;
    }
    if (scelta == NULL)
        scelta = ph_t("Abbinamento non riuscito. Controlla il codice e riprova.");
    if (cap == 0)
        return true;
    g_strlcpy(out, scelta, cap);
    return true;
}

size_t ph_cnx_filtra_codice(const char *in, char *out, size_t cap)
{
    size_t n = 0;
    if (cap == 0)
        return 0;
    if (in != NULL) {
        for (const char *p = in; *p != '\0' && n < PH_CNX_CIFRE && n + 1 < cap; p++)
            if (*p >= '0' && *p <= '9')
                out[n++] = *p;
    }
    out[n] = '\0';
    return n;
}

PhCnxIndirizzo ph_cnx_leggi_indirizzo(const char *testo, char *host, size_t cap, int *porta)
{
    if (testo == NULL)
        return PH_CNX_IND_VUOTO;
    while (*testo == ' ' || *testo == '\t')
        testo++;
    size_t len = strlen(testo);
    while (len > 0 && (testo[len - 1] == ' ' || testo[len - 1] == '\t'))
        len--;
    if (len == 0)
        return PH_CNX_IND_VUOTO;
    if (len > PH_CNX_HOST_MAX + 8)
        return PH_CNX_IND_NON_VALIDO;

    /* Da qui c'e' una copia dell'indirizzo: ha una sola uscita, che la cancella. */
    char copia[PH_CNX_HOST_MAX + 16];
    memcpy(copia, testo, len);
    copia[len] = '\0';
    PhCnxIndirizzo esito = PH_CNX_IND_NON_VALIDO;

    char *h = copia, *p = NULL;
    if (copia[0] == '[') {
        char *chiusa = strchr(copia, ']');
        if (chiusa == NULL || chiusa[1] != ':')
            goto fine;
        *chiusa = '\0';
        h = copia + 1;
        p = chiusa + 2;
        if (strchr(h, ':') == NULL) /* tra parentesi solo IPv6 */
            goto fine;
    } else {
        char *due = strrchr(copia, ':');
        if (due == NULL)
            goto fine;
        *due = '\0';
        p = due + 1;
        if (strchr(h, ':') != NULL) /* IPv6 senza parentesi: ambiguo */
            goto fine;
    }
    if (h[0] == '\0' || strlen(h) >= PH_CNX_HOST_MAX || strlen(h) > 63 || !g_hostname_is_ip_address(h))
        goto fine;
    if (p[0] == '\0' || strlen(p) > 5)
        goto fine;
    int valore = 0;
    for (const char *q = p; *q != '\0'; q++) {
        if (*q < '0' || *q > '9')
            goto fine;
        valore = valore * 10 + (*q - '0');
    }
    if (valore < 1 || valore > 65535)
        goto fine;
    if (host != NULL) {
        if (cap == 0 || strlen(h) >= cap)
            goto fine;
        g_strlcpy(host, h, cap);
    }
    if (porta != NULL)
        *porta = valore;
    esito = PH_CNX_IND_VALIDO;
fine:
    ph_cnx_cancella(copia, sizeof copia);
    return esito;
}

bool ph_cnx_abbina_ammesso(const char *codice, PhCnxIndirizzo indirizzo, bool in_corso)
{
    return !in_corso && indirizzo != PH_CNX_IND_NON_VALIDO && ph_srv_codice_valido(codice);
}

void ph_cnx_cancella(void *p, size_t n)
{
    volatile unsigned char *v = p;
    while (n-- > 0)
        *v++ = 0;
}
