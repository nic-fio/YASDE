/* Servizi veri: trasferimenti (5.9.3). La coda (trasferimenti.h) nasce alla
 * prima sessione e vive fino alla chiusura: quando la connessione cade si
 * stacca (ph_tr_riprendi con NULL, prima di restituire il prestito) e al
 * ritorno si riprende con la connessione nuova. Le funzioni della tabella
 * girano sul thread GTK e non bloccano (ph_tr_accoda, annulla ed elenca non
 * parlano col telefono). Nomi e percorsi dei file non vanno nei log. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sv_interno.h"

#if PH_OSSATURA_SERVIZI_VERI

static PhSrvTrStato stato_srv(PhTrStato s)
{
    switch (s) {
    case PH_TR_ATTESA: return PH_SRV_TR_ATTESA;
    case PH_TR_CORSO: return PH_SRV_TR_CORSO;
    case PH_TR_COMPLETATO: return PH_SRV_TR_COMPLETATO;
    case PH_TR_FALLITO: return PH_SRV_TR_FALLITO;
    case PH_TR_ANNULLATO: return PH_SRV_TR_ANNULLATO;
    }
    return PH_SRV_TR_FALLITO;
}

/* Rilegge la coda e la pubblica. Qualunque thread; t valida per tutta la
 * chiamata (la coda si chiude solo quando nessuno la usa piu'). */
static void pubblica(Sv *sv, PhTrasferimenti *t)
{
    PhTrElenco el = {NULL, 0};
    PhErrore e = {{0}};
    if (ph_tr_elenca(t, &el, &e) != 0)
        return;
    PhSrvTrasferimento *v = el.numero > 0 ? calloc(el.numero, sizeof *v) : NULL;
    if (el.numero > 0 && v == NULL) {
        ph_tr_elenco_libera(&el);
        return;
    }
    for (size_t i = 0; i < el.numero; i++) {
        const PhTrVoce *o = &el.voci[i];
        v[i].id = o->id;
        v[i].verso = o->verso == PH_TR_INVIO ? PH_SRV_INVIO : PH_SRV_RICEZIONE;
        v[i].stato = stato_srv(o->stato);
        v[i].nome = strdup(o->nome);
        v[i].origine = strdup(o->origine);
        v[i].destinazione = strdup(o->destinazione);
        v[i].errore = strdup(o->errore);
        v[i].dimensione = o->dimensione;
        v[i].fatti = o->fatti;
        if (!v[i].nome || !v[i].origine || !v[i].destinazione || !v[i].errore) {
            ph_srv_trasferimenti_libera(v, el.numero);
            ph_tr_elenco_libera(&el);
            return;
        }
    }
    size_t n = el.numero;
    ph_tr_elenco_libera(&el);
    ph_servizi_pubblica_trasferimenti(sv->serv, v, n, ph_tr_coda_ferma(t));
}

/* Avviso della coda: sempre dal suo thread di lavoro. Vi si puo' solo
 * rileggere l'elenco (trasferimenti.h). */
static void su_avviso(const PhTrVoce *voce, void *utente)
{
    (void)voce;
    Sv *sv = utente;
    PhTrasferimenti *t = atomic_load(&sv->tr);
    if (t != NULL) /* durante ph_tr_crea ancora NULL: pubblica sv_tr_sessione_nuova */
        pubblica(sv, t);
}

void sv_tr_pubblica(Sv *sv)
{
    PhTrasferimenti *t = atomic_load(&sv->tr);
    if (t != NULL)
        pubblica(sv, t);
}

void sv_tr_sessione_nuova(Sv *sv, SvSessione *s)
{
    PhErrore e = {{0}};
    PhTrasferimenti *t = atomic_load(&sv->tr);
    if (t == NULL) {
        t = ph_tr_crea(s->a, su_avviso, sv, &e);
        if (t == NULL) {
            fprintf(stderr, "YASDE 2.3: coda dei trasferimenti non creata\n");
            return;
        }
        atomic_store(&sv->tr, t);
    } else {
        /* Voci rimaste in attesa: ripartono da sole, e' una decisione. */
        PhTrElenco el = {NULL, 0};
        size_t attese = 0;
        if (ph_tr_elenca(t, &el, &e) == 0) {
            for (size_t i = 0; i < el.numero; i++)
                if (el.voci[i].stato == PH_TR_ATTESA)
                    attese++;
            ph_tr_elenco_libera(&el);
        }
        if (ph_tr_riprendi(t, s->a, &e) != 0) {
            fprintf(stderr, "YASDE 2.3: coda dei trasferimenti non ripresa\n");
        } else if (attese > 0) {
            ph_shell_decisione("trasferimenti ripresi", "connessione tornata, %zu voci in attesa", attese);
        }
    }
    pubblica(sv, t);
}

void sv_tr_teardown(Sv *sv, SvSessione *s, bool chiusura)
{
    (void)s;
    PhTrasferimenti *t = atomic_load(&sv->tr);
    if (t == NULL)
        return;
    PhErrore e = {{0}};
    if (chiusura) {
        /* Annulla la voce in corso (il parziale sparisce) e ferma il thread. */
        atomic_store(&sv->tr, NULL);
        ph_tr_chiudi(t);
    } else if (ph_tr_riprendi(t, NULL, &e) != 0) {
        fprintf(stderr, "YASDE 2.3: coda dei trasferimenti non staccata\n");
    }
}

void sv_tr_chiudi_finale(Sv *sv)
{
    PhTrasferimenti *t = atomic_exchange(&sv->tr, NULL);
    if (t != NULL)
        ph_tr_chiudi(t); /* staccata dalla connessione: nessuna PhAdb in uso */
}

/* ---- Tabella (thread GTK) ---------------------------------------------------------------- */
int sv_tr_accoda(void *b, PhSrvVerso verso, const char *origine, const char *cartella, PhErrore *e)
{
    Sv *sv = b;
    PhTrasferimenti *t = atomic_load(&sv->tr);
    if (t == NULL)
        return ph_errore(e, "%s", ph_t("Nessun telefono collegato"));
    PhErrore dettaglio = {{0}};
    int r = ph_tr_accoda(t, verso == PH_SRV_INVIO ? PH_TR_INVIO : PH_TR_RICEZIONE, origine, cartella, NULL, &dettaglio);
    if (r != 0) {
        /* Il testo del pezzo sotto e' italiano e tecnico: nel log; a chi chiama (e quindi
         * allo schermo) una frase tradotta. */
        fprintf(stderr, "ossatura: trasferimento non accodato: %s\n", dettaglio.testo);
        ph_errore(e, "%s", ph_t("Non riesco ad accodare il trasferimento."));
    }
    /* Accodare non genera avvisi: si rilegge qui. */
    pubblica(sv, t);
    return r;
}

void sv_tr_annulla(void *b, uint64_t id)
{
    Sv *sv = b;
    PhTrasferimenti *t = atomic_load(&sv->tr);
    if (t == NULL)
        return;
    (void)ph_tr_annulla(t, id, NULL);
    pubblica(sv, t);
}

void sv_tr_annulla_tutto(void *b)
{
    Sv *sv = b;
    PhTrasferimenti *t = atomic_load(&sv->tr);
    if (t == NULL)
        return;
    ph_tr_annulla_tutto(t);
    pubblica(sv, t);
}

void sv_tr_rimuovi_finiti(void *b)
{
    Sv *sv = b;
    PhTrasferimenti *t = atomic_load(&sv->tr);
    if (t == NULL)
        return;
    ph_tr_rimuovi_finiti(t);
    pubblica(sv, t);
}

#endif
