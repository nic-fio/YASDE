/* Servizi veri: prima connessione e abbinamento con il codice (servizi.h,
 * collegamento.h: stato NON_ABBINATO, ph_col_abbina / ph_col_abbina_a).
 *
 * L'esito di un abbinamento arriva dal collegamento in un AVVISO (campo
 * esito_abbinamento, non c'e' una funzione di ritorno). La richiesta si
 * registra PRIMA di chiamare ph_col_abbina e la conclude solo l'avviso con
 * l'esito; se non arriva nulla entro il limite (60 s) e' SCADUTA.
 * Una risposta sola per richiesta: chi toglie la richiesta dallo slot (sotto
 * abb_m) e' l'unico a chiudere il lavoro.
 * RISERVATEZZA: il codice e l'indirizzo valgono solo durante la chiamata e
 * vanno direttamente a ph_col_abbina*, che li copia: qui non si copiano, non si
 * scrivono nei log e non si salvano. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sv_interno.h"

#if PH_OSSATURA_SERVIZI_VERI

int sv_prova_abb_limite_s = PH_SRV_ABB_LIMITE_S;

typedef struct SvAbbPend {
    atomic_int rif;       /* slot + timer */
    PhSrvLavoro *l;
    GSource *src;         /* il timer del limite (un riferimento nostro) */
} SvAbbPend;

static void pend_lascia(SvAbbPend *p)
{
    if (atomic_fetch_sub(&p->rif, 1) == 1)
        g_free(p);
}

/* Toglie la richiesta dallo slot (se e' ancora quella) e la tiene per chi chiude. */
static SvAbbPend *pend_prendi(Sv *sv, SvAbbPend *quale)
{
    g_mutex_lock(&sv->abb_m);
    SvAbbPend *p = sv->abb;
    if (p != NULL && (quale == NULL || quale == p))
        sv->abb = NULL;
    else
        p = NULL;
    g_mutex_unlock(&sv->abb_m);
    return p;
}

static void pend_chiudi(SvAbbPend *p, PhSrvAbbEsito esito, const char *frase)
{
    ph_servizi_lavoro_abbina(p->l, esito, frase);
    /* Il timer non serve piu' (g_source_destroy e' sicura da qualunque thread e
     * anche se la sorgente e' gia' stata tolta). */
    g_source_destroy(p->src);
    g_source_unref(p->src);
    pend_lascia(p); /* il riferimento dello slot */
}

typedef struct {
    Sv *sv;
    SvAbbPend *p;
} Timer;

static gboolean timer_scaduto(gpointer dati)
{
    Timer *t = dati;
    SvAbbPend *p = pend_prendi(t->sv, t->p);
    if (p != NULL) /* nessun esito entro il limite */
        pend_chiudi(p, PH_SRV_ABB_SCADUTA, NULL);
    return G_SOURCE_REMOVE;
}

static void timer_libera(gpointer dati)
{
    Timer *t = dati;
    pend_lascia(t->p);
    sv_rif_lascia(t->sv);
    g_free(t);
}

void sv_abbina(void *b, PhSrvLavoro *l, const char *codice, const char *host, int porta)
{
    if (b == NULL) { /* dopo ph_servizi_libera */
        ph_servizi_lavoro_abbina(l, PH_SRV_ABB_RIFIUTATA, NULL);
        return;
    }
    Sv *sv = b;
    if (atomic_load(&sv->chiusura) || sv->col == NULL) {
        ph_servizi_lavoro_abbina(l, PH_SRV_ABB_RIFIUTATA, NULL);
        return;
    }
    SvAbbPend *p = g_new0(SvAbbPend, 1);
    atomic_init(&p->rif, 2); /* slot + timer */
    p->l = l;
    /* Il timer si crea PRIMA di pubblicare la richiesta: chi la conclude ha
     * sempre il suo riferimento alla sorgente da distruggere. */
    Timer *t = g_new0(Timer, 1);
    sv_rif_prendi(sv);
    t->sv = sv;
    t->p = p;
    p->src = g_timeout_source_new_seconds((guint)sv_prova_abb_limite_s);
    g_source_set_callback(p->src, timer_scaduto, t, timer_libera);
    g_source_attach(p->src, NULL);
    g_mutex_lock(&sv->abb_m);
    bool occupato = sv->abb != NULL;
    if (!occupato)
        sv->abb = p;
    g_mutex_unlock(&sv->abb_m);
    if (occupato) {
        g_source_destroy(p->src); /* libera il riferimento del timer */
        g_source_unref(p->src);
        pend_lascia(p);           /* quello dello slot, mai occupato */
        ph_servizi_lavoro_abbina(l, PH_SRV_ABB_RIFIUTATA, ph_t("Un altro abbinamento e' gia' in corso."));
        return;
    }

    /* Sul thread GTK: ph_col_abbina non blocca. A errore e' stato rifiutato:
     * non si e' in NON_ABBINATO, o ce n'e' gia' uno, o argomenti non validi. */
    PhErrore e = {{0}};
    int r = host != NULL ? ph_col_abbina_a(sv->col, host, (uint16_t)porta, codice, &e)
                         : ph_col_abbina(sv->col, codice, &e);
    if (r != 0) {
        SvAbbPend *mio = pend_prendi(sv, p);
        if (mio != NULL)
            pend_chiudi(mio, PH_SRV_ABB_RIFIUTATA, ph_t("Non posso abbinare adesso: riprova tra un momento."));
    }
}

void sv_abbina_avviso(Sv *sv, const PhColAvviso *a)
{
    /* Conclude SOLO l'avviso che porta l'esito di una richiesta accettata
     * (esito_abbinamento: uno e uno solo per richiesta): un CERCO spontaneo (per
     * esempio un "cerca di nuovo" pubblicato dopo) o qualunque altro avviso non
     * conclude nulla. La frase di un fallimento e' del collegamento, senza codice
     * ne' indirizzi. */
    PhSrvAbbEsito esito;
    const char *frase = NULL;
    switch (a->esito_abbinamento) {
    case PH_COL_ABB_RIUSCITO:
        esito = PH_SRV_ABB_RIUSCITO;
        break;
    case PH_COL_ABB_FALLITO:
        esito = PH_SRV_ABB_FALLITO;
        frase = a->motivo[0] != '\0' ? a->motivo : NULL;
        break;
    case PH_COL_ABB_NESSUNO:
    default:
        return;
    }
    SvAbbPend *p = pend_prendi(sv, NULL);
    if (p != NULL)
        pend_chiudi(p, esito, frase);
}

void sv_abbina_chiudi(Sv *sv)
{
    SvAbbPend *p = pend_prendi(sv, NULL);
    if (p != NULL)
        pend_chiudi(p, PH_SRV_ABB_FALLITO, NULL); /* la risposta si scarta: i servizi si chiudono */
}

int sv_telefoni_noti(void)
{
    PhCfgTelefoni *t = malloc(sizeof *t);
    if (t == NULL)
        return -1;
    PhErrore e = {{0}};
    int n = -1;
    if (ph_cfg_telefoni_carica(t, &e) >= 0)
        n = (int)t->n;
    free(t);
    return n;
}

#endif
