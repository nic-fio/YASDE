/* Logica pura del widget Interruttori rapidi (logica_controlli.h). */
#include "logica_controlli.h"

#include <stdio.h>
#include <string.h>

#include "config/lingua.h"

/* Righe del nome: 1 se sta intero nella colonna, 2 se le parole stanno e le due righe bastano,
 * 0 se nemmeno cosi' (niente testo: mai un nome tagliato). Senza misure vere si presume una riga. */
static int righe_del_nome(int colonna, const PhDskCtlMisure *m)
{
    if (m == NULL || m->nome_largo <= 0)
        return 1;
    int utile = colonna - PH_DSK_CTL_MARGINE_TESTO;
    if (m->nome_largo <= utile)
        return 1;
    /* A capo tra le parole: la piu' larga deve entrare, e il resto in due righe (con un po' di
     * scarto, perche' il punto di a capo non e' mai esatto). */
    if (m->parola_larga <= utile && m->nome_largo <= 2 * utile - 8)
        return 2;
    return 0;
}

void ph_dsk_ctl_disposizione(int larghezza, int altezza, const PhDskCtlMisure *m, PhDskCtlDisposizione *out)
{
    memset(out, 0, sizeof *out);
    if (larghezza <= 0 || altezza <= 0)
        return;
    int riga = m != NULL && m->riga > 0 ? m->riga : PH_DSK_CTL_RIGA;
    int riserva = m != NULL && m->riserva_v > 0 ? m->riserva_v : 0;
    int colonna = larghezza / 3;
    int righe = colonna >= PH_DSK_CTL_COL_MIN ? righe_del_nome(colonna, m) : 0;
    int blocco = PH_DSK_CTL_LATO + PH_DSK_CTL_GAP + (righe + 1) * riga;
    if (righe > 0 && altezza >= blocco && (altezza - blocco) / 2 + riserva >= PH_DSK_CTL_MARGINE_V) {
        out->con_testo = true;
        out->righe_nome = righe;
        out->riga = riga;
        out->diametro = PH_DSK_CTL_LATO;
        out->largo = colonna;
        out->alto = blocco;
        out->spazio = 0;
        out->x = (larghezza - 3 * out->largo) / 2;
        out->y = (altezza - blocco) / 2;
        return;
    }
    int d = PH_DSK_CTL_LATO_SOLO;
    if (d > altezza)
        d = altezza;
    if (3 * d + 2 * PH_DSK_CTL_SPAZIO_MIN > larghezza)
        d = (larghezza - 2 * PH_DSK_CTL_SPAZIO_MIN) / 3;
    if (d < 0)
        d = 0;
    int spazio = (larghezza - 3 * d) / 4;
    if (spazio < PH_DSK_CTL_SPAZIO_MIN)
        spazio = PH_DSK_CTL_SPAZIO_MIN;
    if (spazio > PH_DSK_CTL_SPAZIO_MAX)
        spazio = PH_DSK_CTL_SPAZIO_MAX;
    out->riga = riga;
    out->diametro = d;
    out->largo = d;
    out->alto = d;
    out->spazio = spazio;
    int totale = 3 * d + 2 * spazio;
    out->x = (larghezza - totale) / 2;
    if (out->x < 0)
        out->x = 0;
    out->y = (altezza - d) / 2;
}

PhDskCtlAspetto ph_dsk_ctl_aspetto(bool noto, int controllo, int valore, const PhDskInterruttore *ii)
{
    if (!noto)
        return PH_DSK_CTL_ATTESA_DATI;
    if (!ii->disponibile)
        return PH_DSK_CTL_NON_DISPONIBILE;
    if (ii->in_corso)
        return PH_DSK_CTL_IN_CORSO;
    if (controllo == 0) {
        if (valore == PH_SRV_SUONERIA_SUONA)
            return PH_DSK_CTL_ATTIVO;
        return valore == PH_SRV_SUONERIA_VIBRA ? PH_DSK_CTL_BORDO : PH_DSK_CTL_SPENTO;
    }
    return ii->attivo ? PH_DSK_CTL_ATTIVO : PH_DSK_CTL_SPENTO;
}

PhDskCtlIcona ph_dsk_ctl_icona(int controllo, int valore)
{
    if (controllo == 1)
        return PH_DSK_CTL_ICONA_LUNA;
    if (controllo == 2)
        return PH_DSK_CTL_ICONA_TORCIA;
    return valore == PH_SRV_SUONERIA_VIBRA ? PH_DSK_CTL_ICONA_VIBRA
           : valore == PH_SRV_SUONERIA_SILENZIOSO ? PH_DSK_CTL_ICONA_SILENZIOSO
                                                  : PH_DSK_CTL_ICONA_SUONA;
}

void ph_dsk_ctl_testi(bool noto, const PhDskInterruttore *ii, char *etichetta, size_t cap_e, char *suggerimento,
                      size_t cap_s)
{
    if (cap_e > 0)
        ph_lingua_componi(etichetta, cap_e, ph_t("{titolo}: {testo}"), "titolo", ii->etichetta, "testo",
                          noto ? ii->stato : ph_t("In corso"), (const char *)NULL);
    if (cap_s > 0) {
        if (!noto)
            suggerimento[0] = '\0'; /* niente da dire prima della prima risposta */
        else
            ph_lingua_componi(suggerimento, cap_s, ph_t("{titolo}: {testo}"), "titolo", ii->etichetta, "testo",
                              ii->disponibile ? ii->stato : ph_t("Non disponibile su questo telefono"),
                              (const char *)NULL);
    }
}
