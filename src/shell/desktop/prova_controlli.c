/* Prove di logica del widget Interruttori rapidi (logica_controlli.h), SENZA schermo:
 * disposizione dei tondi secondo la misura vera, aspetto di ogni stato, icone, testi. */
#include <stdio.h>
#include <string.h>

#include "config/lingua.h"
#include "logica_controlli.h"

static int errori = 0;
#define VERIFICA(cond)                                                                       \
    do {                                                                                     \
        if (!(cond)) {                                                                       \
            fprintf(stderr, "%s:%d: fallita: %s\n", __FILE__, __LINE__, #cond);              \
            errori++;                                                                        \
        }                                                                                    \
    } while (0)
#define VERIFICA_STR(a, b)                                                                   \
    do {                                                                                     \
        if (strcmp((a), (b)) != 0) {                                                         \
            fprintf(stderr, "%s:%d: \"%s\" != \"%s\"\n", __FILE__, __LINE__, (a), (b));      \
            errori++;                                                                        \
        }                                                                                    \
    } while (0)

/* I tasti stanno dentro l'area, non si sovrappongono e sono centrati. */
static void controlla_dentro(const PhDskCtlDisposizione *d, int l, int a)
{
    int fine = d->x + 3 * d->largo + 2 * d->spazio;
    VERIFICA(d->x >= 0 && d->y >= 0);
    VERIFICA(fine <= l);
    VERIFICA(d->y + d->alto <= a);
    VERIFICA(d->spazio >= 0);
}

static void prova_disposizione(void)
{
    PhDskCtlDisposizione d;
    /* Misure di testo tipiche: riga 16, «Do not disturb» largo 78 con la parola piu' larga 40,
     * imbottitura verticale della capsula 10. */
    PhDskCtlMisure m = {.riga = 16, .nome_largo = 78, .parola_larga = 40, .riserva_v = 10};

    /* Misura minima della capsula (200x72 meno l'imbottitura 24/10): solo i tre tondi come D2. */
    ph_dsk_ctl_disposizione(152, 52, &m, &d);
    VERIFICA(!d.con_testo && d.righe_nome == 0);
    VERIFICA(d.diametro == PH_DSK_CTL_LATO_SOLO);
    VERIFICA(d.largo == d.diametro && d.alto == d.diametro);
    VERIFICA(d.spazio >= PH_DSK_CTL_SPAZIO_MIN);
    controlla_dentro(&d, 152, 52);
    VERIFICA(d.y == (52 - d.diametro) / 2); /* centrati in altezza */

    /* Il nome non sta su una riga (colonna 67): va su DUE righe e il blocco e' piu' alto. In 76 di
     * altezza non ci sta: tondi soli, mai un nome tagliato. */
    ph_dsk_ctl_disposizione(202, 76, &m, &d);
    VERIFICA(!d.con_testo);
    controlla_dentro(&d, 202, 76);
    /* Con l'altezza che serve (4 + 3 righe) il testo c'e', su due righe, e il blocco e' centrato. */
    int blocco2 = PH_DSK_CTL_LATO + PH_DSK_CTL_GAP + 3 * 16;
    ph_dsk_ctl_disposizione(202, blocco2, &m, &d);
    VERIFICA(d.con_testo && d.righe_nome == 2);
    VERIFICA(d.diametro == PH_DSK_CTL_LATO && d.alto == blocco2 && d.riga == 16);
    VERIFICA(d.largo == 202 / 3);
    controlla_dentro(&d, 202, blocco2);
    ph_dsk_ctl_disposizione(202, blocco2 - 1, &m, &d);
    VERIFICA(!d.con_testo); /* un pixel in meno: niente testo */

    /* Grande (320x112, area 272x92): il nome sta su una riga, il blocco e' centrato con 8 px sopra. */
    ph_dsk_ctl_disposizione(272, 92, &m, &d);
    VERIFICA(d.con_testo && d.righe_nome == 1);
    VERIFICA(d.alto == PH_DSK_CTL_LATO + PH_DSK_CTL_GAP + 2 * 16);
    VERIFICA(d.y == (92 - d.alto) / 2 && d.y >= 8);
    controlla_dentro(&d, 272, 92);

    /* Un nome che non entra nemmeno a capo (parola troppo larga): niente testo, anche se alto. */
    PhDskCtlMisure larga = m;
    larga.nome_largo = 300;
    larga.parola_larga = 200;
    ph_dsk_ctl_disposizione(272, 200, &larga, &d);
    VERIFICA(!d.con_testo && d.righe_nome == 0);
    PhDskCtlMisure lunga = m;
    lunga.nome_largo = 400; /* parole corte ma troppo testo per due righe */
    ph_dsk_ctl_disposizione(272, 200, &lunga, &d);
    VERIFICA(!d.con_testo);

    /* Margine: 8 px liberi sopra e sotto contati dal bordo della capsula (imbottitura inclusa). */
    int blocco1 = PH_DSK_CTL_LATO + PH_DSK_CTL_GAP + 2 * 16;
    PhDskCtlMisure senza = m;
    senza.riserva_v = 0;
    ph_dsk_ctl_disposizione(272, blocco1 + 16, &senza, &d);
    VERIFICA(d.con_testo && d.y == 8); /* 8 px esatti */
    ph_dsk_ctl_disposizione(272, blocco1 + 15, &senza, &d);
    VERIFICA(!d.con_testo); /* 7 px: troppo poco */
    ph_dsk_ctl_disposizione(272, blocco1, &m, &d);
    VERIFICA(d.con_testo && d.y == 0); /* l'imbottitura da 10 px vale come margine */
    senza.riserva_v = 7;
    ph_dsk_ctl_disposizione(272, blocco1, &senza, &d);
    VERIFICA(!d.con_testo);

    /* Soglia in larghezza: tre colonne da COL_MIN. */
    PhDskCtlMisure corto = {.riga = 16, .nome_largo = 30, .parola_larga = 30, .riserva_v = 10};
    ph_dsk_ctl_disposizione(3 * PH_DSK_CTL_COL_MIN, blocco1, &corto, &d);
    VERIFICA(d.con_testo);
    ph_dsk_ctl_disposizione(3 * PH_DSK_CTL_COL_MIN - 1, blocco1, &corto, &d);
    VERIFICA(!d.con_testo);
    controlla_dentro(&d, 3 * PH_DSK_CTL_COL_MIN - 1, blocco1);

    /* Riga piu' alta del previsto: il blocco cresce e la soglia con lui. */
    PhDskCtlMisure alta = corto;
    alta.riga = 20;
    ph_dsk_ctl_disposizione(272, blocco1, &alta, &d);
    VERIFICA(!d.con_testo);
    ph_dsk_ctl_disposizione(272, PH_DSK_CTL_LATO + PH_DSK_CTL_GAP + 2 * 20, &alta, &d);
    VERIFICA(d.con_testo && d.riga == 20 && d.alto == PH_DSK_CTL_LATO + PH_DSK_CTL_GAP + 2 * 20);

    /* Senza misure vere (NULL): una riga e riga di riserva. */
    ph_dsk_ctl_disposizione(272, blocco1 + 16, NULL, &d);
    VERIFICA(d.con_testo && d.righe_nome == 1 && d.riga == PH_DSK_CTL_RIGA);

    /* Alto ma stretto: i tondi non escono. */
    ph_dsk_ctl_disposizione(120, 200, &m, &d);
    VERIFICA(!d.con_testo);
    controlla_dentro(&d, 120, 200);
    VERIFICA(3 * d.diametro + 2 * d.spazio <= 120);

    /* Bassissimo: il tondo si rimpicciolisce all'altezza. */
    ph_dsk_ctl_disposizione(300, 30, &m, &d);
    VERIFICA(!d.con_testo && d.diametro == 30);
    controlla_dentro(&d, 300, 30);

    /* Larghissimo: lo spazio e' limitato, i tre restano uniti e centrati. */
    ph_dsk_ctl_disposizione(2000, 52, &m, &d);
    VERIFICA(d.spazio == PH_DSK_CTL_SPAZIO_MAX);
    VERIFICA(d.x == (2000 - (3 * d.diametro + 2 * d.spazio)) / 2);

    /* Misure assurde: disposizione vuota, mai valori negativi. */
    ph_dsk_ctl_disposizione(0, 52, &m, &d);
    VERIFICA(d.diametro == 0 && !d.con_testo);
    ph_dsk_ctl_disposizione(100, -5, &m, &d);
    VERIFICA(d.diametro == 0 && !d.con_testo);
    ph_dsk_ctl_disposizione(10, 10, &m, &d);
    VERIFICA(d.diametro >= 0 && d.x >= 0 && d.largo >= 0);
    ph_dsk_ctl_disposizione(1, 1, NULL, &d);
    VERIFICA(d.diametro >= 0 && d.x >= 0);
}

static void prova_aspetto(void)
{
    PhDskInterruttore ii;
    ph_dsk_interruttore(2, 0, true, false, &ii);
    VERIFICA(ph_dsk_ctl_aspetto(true, 2, 0, &ii) == PH_DSK_CTL_SPENTO);
    ph_dsk_interruttore(2, 1, true, false, &ii);
    VERIFICA(ph_dsk_ctl_aspetto(true, 2, 1, &ii) == PH_DSK_CTL_ATTIVO);
    ph_dsk_interruttore(1, 1, true, false, &ii);
    VERIFICA(ph_dsk_ctl_aspetto(true, 1, 1, &ii) == PH_DSK_CTL_ATTIVO);
    ph_dsk_interruttore(1, 0, true, false, &ii);
    VERIFICA(ph_dsk_ctl_aspetto(true, 1, 0, &ii) == PH_DSK_CTL_SPENTO);
    ph_dsk_interruttore(2, 1, true, true, &ii); /* dopo un clic: «in corso», mai lo stato presunto */
    VERIFICA(ph_dsk_ctl_aspetto(true, 2, 1, &ii) == PH_DSK_CTL_IN_CORSO);
    ph_dsk_interruttore(2, PH_SRV_NON_DISPONIBILE, true, false, &ii);
    VERIFICA(ph_dsk_ctl_aspetto(true, 2, PH_SRV_NON_DISPONIBILE, &ii) == PH_DSK_CTL_NON_DISPONIBILE);
    ph_dsk_interruttore(2, 1, false, false, &ii); /* comando non permesso: inattivo anche con un valore */
    VERIFICA(ph_dsk_ctl_aspetto(true, 2, 1, &ii) == PH_DSK_CTL_NON_DISPONIBILE);
    /* Suoneria: «Suona» = pieno d'accento, «Vibra» = solo bordo, «Silenzioso» = spento. */
    ph_dsk_interruttore(0, PH_SRV_SUONERIA_SUONA, true, false, &ii);
    VERIFICA(ph_dsk_ctl_aspetto(true, 0, PH_SRV_SUONERIA_SUONA, &ii) == PH_DSK_CTL_ATTIVO);
    ph_dsk_interruttore(0, PH_SRV_SUONERIA_VIBRA, true, false, &ii);
    VERIFICA(ph_dsk_ctl_aspetto(true, 0, PH_SRV_SUONERIA_VIBRA, &ii) == PH_DSK_CTL_BORDO);
    ph_dsk_interruttore(0, PH_SRV_SUONERIA_SILENZIOSO, true, false, &ii);
    VERIFICA(ph_dsk_ctl_aspetto(true, 0, PH_SRV_SUONERIA_SILENZIOSO, &ii) == PH_DSK_CTL_SPENTO);
    ph_dsk_interruttore(0, PH_SRV_SUONERIA_SUONA, true, true, &ii); /* in corso vince anche sulla suoneria */
    VERIFICA(ph_dsk_ctl_aspetto(true, 0, PH_SRV_SUONERIA_SUONA, &ii) == PH_DSK_CTL_IN_CORSO);
    ph_dsk_interruttore(0, PH_SRV_NON_DISPONIBILE, true, false, &ii);
    VERIFICA(ph_dsk_ctl_aspetto(true, 0, PH_SRV_NON_DISPONIBILE, &ii) == PH_DSK_CTL_NON_DISPONIBILE);
    ph_dsk_interruttore(0, PH_SRV_SUONERIA_VIBRA, true, false, &ii);
    VERIFICA(ph_dsk_ctl_aspetto(false, 0, PH_SRV_SUONERIA_VIBRA, &ii) == PH_DSK_CTL_ATTESA_DATI); /* nessuno stato presunto */
    /* Gli aspetti sono tutti distinti: i soli tondi li fanno riconoscere. */
    PhDskCtlAspetto tutti[] = {PH_DSK_CTL_SPENTO, PH_DSK_CTL_ATTIVO, PH_DSK_CTL_IN_CORSO,
                               PH_DSK_CTL_NON_DISPONIBILE, PH_DSK_CTL_ATTESA_DATI, PH_DSK_CTL_BORDO};
    for (size_t i = 0; i < sizeof tutti / sizeof tutti[0]; i++)
        for (size_t k = i + 1; k < sizeof tutti / sizeof tutti[0]; k++)
            VERIFICA(tutti[i] != tutti[k]);
}

static void prova_icone(void)
{
    /* La suoneria cambia icona con i suoi tre stati; gli altri due hanno la loro. */
    VERIFICA(ph_dsk_ctl_icona(0, PH_SRV_SUONERIA_SUONA) == PH_DSK_CTL_ICONA_SUONA);
    VERIFICA(ph_dsk_ctl_icona(0, PH_SRV_SUONERIA_VIBRA) == PH_DSK_CTL_ICONA_VIBRA);
    VERIFICA(ph_dsk_ctl_icona(0, PH_SRV_SUONERIA_SILENZIOSO) == PH_DSK_CTL_ICONA_SILENZIOSO);
    VERIFICA(ph_dsk_ctl_icona(0, PH_SRV_NON_DISPONIBILE) == PH_DSK_CTL_ICONA_SUONA);
    VERIFICA(ph_dsk_ctl_icona(1, 0) == PH_DSK_CTL_ICONA_LUNA && ph_dsk_ctl_icona(1, 1) == PH_DSK_CTL_ICONA_LUNA);
    VERIFICA(ph_dsk_ctl_icona(2, 0) == PH_DSK_CTL_ICONA_TORCIA && ph_dsk_ctl_icona(2, 1) == PH_DSK_CTL_ICONA_TORCIA);
}

static void prova_testi(void)
{
    ph_lingua_imposta(PH_LINGUA_EN);
    PhDskInterruttore ii;
    char et[160], sug[160];
    /* Nei soli tondi lo stato sta nell'etichetta accessibile e nel suggerimento. */
    ph_dsk_interruttore(0, PH_SRV_SUONERIA_VIBRA, true, false, &ii);
    ph_dsk_ctl_testi(true, &ii, et, sizeof et, sug, sizeof sug);
    VERIFICA_STR(et, "Ringer: Vibrates");
    VERIFICA_STR(sug, "Ringer: Vibrates");
    ph_dsk_interruttore(0, PH_SRV_SUONERIA_SUONA, true, false, &ii);
    ph_dsk_ctl_testi(true, &ii, et, sizeof et, sug, sizeof sug);
    VERIFICA_STR(et, "Ringer: Rings");
    ph_dsk_interruttore(0, PH_SRV_SUONERIA_SILENZIOSO, true, false, &ii);
    ph_dsk_ctl_testi(true, &ii, et, sizeof et, sug, sizeof sug);
    VERIFICA_STR(et, "Ringer: Silent");
    ph_dsk_interruttore(1, 1, true, false, &ii);
    ph_dsk_ctl_testi(true, &ii, et, sizeof et, sug, sizeof sug);
    VERIFICA_STR(et, "Do not disturb: On");
    ph_dsk_interruttore(2, 0, true, false, &ii);
    ph_dsk_ctl_testi(true, &ii, et, sizeof et, sug, sizeof sug);
    VERIFICA_STR(et, "Flashlight: Off");
    /* Dopo un clic: «In progress». */
    ph_dsk_interruttore(2, 1, true, true, &ii);
    ph_dsk_ctl_testi(true, &ii, et, sizeof et, sug, sizeof sug);
    VERIFICA_STR(et, "Flashlight: In progress");
    VERIFICA_STR(sug, "Flashlight: In progress");
    /* Non disponibile: etichetta breve, suggerimento con la spiegazione. */
    ph_dsk_interruttore(2, PH_SRV_NON_DISPONIBILE, true, false, &ii);
    ph_dsk_ctl_testi(true, &ii, et, sizeof et, sug, sizeof sug);
    VERIFICA_STR(et, "Flashlight: Not available");
    VERIFICA_STR(sug, "Flashlight: Not available on this phone");
    /* Prima della prima risposta: niente stato presunto, nessun suggerimento. */
    ph_dsk_interruttore(2, 1, true, false, &ii);
    ph_dsk_ctl_testi(false, &ii, et, sizeof et, sug, sizeof sug);
    VERIFICA_STR(et, "Flashlight: In progress");
    VERIFICA_STR(sug, "");
    /* Buffer minuscoli: mai scritture oltre la fine. */
    char piccolo[4];
    ph_dsk_ctl_testi(true, &ii, piccolo, sizeof piccolo, piccolo, 0);
    VERIFICA(strlen(piccolo) < sizeof piccolo);
    ph_lingua_imposta(PH_LINGUA_IT);
}

int main(void)
{
    ph_lingua_imposta(PH_LINGUA_IT);
    prova_disposizione();
    prova_aspetto();
    prova_icone();
    prova_testi();
    if (errori == 0)
        printf("prova_controlli: tutto bene\n");
    return errori == 0 ? 0 : 1;
}
