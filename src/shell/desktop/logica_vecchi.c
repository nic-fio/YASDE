/* Logica pura dei widget «vecchi» rifatti (logica_vecchi.h): solo scelte e misure. */
#include "logica_vecchi.h"

#include <glib.h>
#include <string.h>

#include "logica_comp.h"

/* ---- Spazio utile ---------------------------------------------------------------------------------- */
void ph_dsk_vecchi_utile(int larghezza, int altezza, int *utile_l, int *utile_a)
{
    long l = (long)larghezza - 2 * PH_DSK_VECCHI_PAD_O, a = (long)altezza - 2 * PH_DSK_VECCHI_PAD_V;
    if (utile_l != NULL)
        *utile_l = l < 0 ? 0 : (l > 100000 ? 100000 : (int)l);
    if (utile_a != NULL)
        *utile_a = a < 0 ? 0 : (a > 100000 ? 100000 : (int)a);
}

/* ---- Memoria ------------------------------------------------------------------------------------------ */
#define MEM_NUMERO_MIN 40    /* etichetta e numero: sotto questo il numero e' troppo piccolo */
#define MEM_NUMERO_MAX 62    /* oltre non cresce: il corpo del numero si ferma a 44 px */
#define MEM_NOTA_SOTTO 16    /* la dicitura sotto il numero (12 px) */
#define MEM_NOTA_ACCANTO 260 /* spazio utile da cui la dicitura sta accanto al numero */
#define MEM_BARRA 24         /* barra (14) e il suo respiro (10) */
#define MEM_VOCE 18          /* una voce della legenda */
#define MEM_VOCE_SPAZIO 6
#define MEM_LEGENDA_RESPIRO 12

PhDskMemoriaDisp ph_dsk_memoria_disp(int larghezza, int altezza, size_t n_voci, int colonne)
{
    int ul, ua;
    ph_dsk_vecchi_utile(larghezza, altezza, &ul, &ua);
    PhDskMemoriaDisp d = {false, false, ul >= MEM_NOTA_ACCANTO, 0};
    long sotto = d.nota_accanto ? 0 : MEM_NOTA_SOTTO;
    d.barra = ua >= MEM_NUMERO_MIN + sotto + MEM_BARRA;
    long alto = 0;
    if (d.barra && n_voci > 0 && n_voci <= 1000) { /* tante voci non ci stanno in nessuna misura */
        size_t cols = colonne < 1 ? 1 : (size_t)colonne;
        size_t righe = (n_voci + cols - 1) / cols;
        alto = (long)righe * MEM_VOCE + (long)(righe - 1) * MEM_VOCE_SPAZIO + MEM_LEGENDA_RESPIRO;
        d.legenda = ua >= MEM_NUMERO_MIN + sotto + MEM_BARRA + alto;
    }
    long libero = ua - sotto - (d.barra ? MEM_BARRA : 0) - (d.legenda ? alto : 0);
    d.numero_alto = libero < MEM_NUMERO_MIN ? MEM_NUMERO_MIN : (libero > MEM_NUMERO_MAX ? MEM_NUMERO_MAX : (int)libero);
    return d;
}

size_t ph_dsk_memoria_fette(const PhSrvMemoriaDettaglio *det, uint64_t usata, PhDskMemFetta *out)
{
    size_t n = 0;
    if (det == NULL || out == NULL)
        return 0;
    const PhSrvVoceMemoria *voci[4] = {&det->immagini, &det->video, &det->audio, &det->app};
    uint64_t somma = 0;
    bool letta = false;
    for (int i = 0; i < 4; i++) {
        if (!voci[i]->leggibile)
            continue;
        letta = true;
        if (voci[i]->byte == 0)
            continue;
        out[n].categoria = (PhDskMemCategoria)i;
        out[n].byte = voci[i]->byte;
        n++;
        somma = voci[i]->byte > UINT64_MAX - somma ? UINT64_MAX : somma + voci[i]->byte; /* dato ostile */
    }
    if (letta && usata > somma) {
        out[n].categoria = PH_DSK_MEM_ALTRO;
        out[n].byte = usata - somma;
        n++;
    }
    return n;
}

int ph_dsk_memoria_accento(PhDskMemCategoria c)
{
    switch (c) {
    case PH_DSK_MEM_IMMAGINI: return PH_DSK_ACC_TEMA;
    case PH_DSK_MEM_VIDEO: return PH_DSK_ACC_INDACO;
    case PH_DSK_MEM_AUDIO: return PH_DSK_ACC_AMBRA;
    case PH_DSK_MEM_APP: return PH_DSK_ACC_CORALLO;
    default: return PH_DSK_ACC_ARDESIA;
    }
}

const char *ph_dsk_pacchetto_impostazioni(void)
{
    return "com.android.settings";
}

PhShellPagina ph_dsk_widget_pagina(PhCfgWidgetTipo tipo)
{
    switch (tipo) {
    case PH_CFG_WIDGET_BATTERIA: return PH_SHELL_PAGINA_BATTERIA;
    case PH_CFG_WIDGET_MEMORIA: return PH_SHELL_PAGINA_MEMORIA;
    default: return PH_SHELL_PAGINA_NESSUNA;
    }
}

/* ---- Notifiche -------------------------------------------------------------------------------------- */
#define NOT_TITOLO 22 /* titolo del widget (12 px) e il suo respiro */
#define NOT_RIGA 44   /* tondo di 32 px e imbottitura del pulsante */
#define NOT_SPAZIO 2

size_t ph_dsk_notifiche_righe_max(int larghezza, int altezza)
{
    int ul, ua;
    ph_dsk_vecchi_utile(larghezza, altezza, &ul, &ua);
    (void)ul;
    long n = ((long)ua - NOT_TITOLO + NOT_SPAZIO) / (NOT_RIGA + NOT_SPAZIO);
    return n < 1 ? 1 : (size_t)n;
}

/* ---- Calendario ---------------------------------------------------------------------------------------- */
#define CAL_COLONNA_MIN 200 /* spazio utile da cui il giorno sta a sinistra (colonna di 64 e 14 di spazio) */
#define CAL_DATA 22         /* la riga della data, quando il giorno non sta a sinistra */
#define CAL_IMPEGNO 40
#define CAL_SPAZIO 2

PhDskCalDisp ph_dsk_calendario_disp(int larghezza, int altezza)
{
    int ul, ua;
    ph_dsk_vecchi_utile(larghezza, altezza, &ul, &ua);
    PhDskCalDisp d;
    d.colonna_giorno = ul >= CAL_COLONNA_MIN;
    long libero = ua - (d.colonna_giorno ? 0 : CAL_DATA);
    long n = (libero + CAL_SPAZIO) / (CAL_IMPEGNO + CAL_SPAZIO);
    d.max_impegni = n < 1 ? 1 : (size_t)n;
    return d;
}

bool ph_dsk_impegno_in_corso(const PhDskEvento *e, uint64_t adesso_ms)
{
    return e != NULL && !e->tutto_il_giorno && e->fine_ms > e->inizio_ms && e->inizio_ms <= adesso_ms &&
           adesso_ms < e->fine_ms;
}

/* ---- Stato --------------------------------------------------------------------------------------------- */
#define STATO_FISSO 58 /* titolo, riga principale e barra */
#define STATO_RIGA 24

size_t ph_dsk_stato_righe(int larghezza, int altezza)
{
    int ul, ua;
    ph_dsk_vecchi_utile(larghezza, altezza, &ul, &ua);
    (void)ul;
    long n = ((long)ua - STATO_FISSO) / STATO_RIGA;
    return n < 0 ? 0 : (n > 3 ? 3 : (size_t)n);
}

bool ph_dsk_pacchetto_valido(const char *pacchetto)
{
    if (pacchetto == NULL || pacchetto[0] == '\0' || strlen(pacchetto) > 255 ||
        !g_utf8_validate(pacchetto, -1, NULL))
        return false;
    for (const unsigned char *p = (const unsigned char *)pacchetto; *p != '\0'; p++)
        if (*p < 0x20 || *p == 0x7f)
            return false;
    return true;
}

/* ---- Accenti e testi ------------------------------------------------------------------------------------- */
int ph_dsk_accento_pacchetto(const char *pacchetto)
{
    if (pacchetto == NULL || pacchetto[0] == '\0')
        return PH_DSK_ACC_ARDESIA;
    uint32_t h = 2166136261u; /* FNV-1a: stesso pacchetto, stesso colore, sempre */
    for (const unsigned char *p = (const unsigned char *)pacchetto; *p != '\0'; p++)
        h = (h ^ *p) * 16777619u;
    return 1 + (int)(h % (PH_DSK_ACC_N - 1));
}

int ph_dsk_accento_impegno(size_t i)
{
    /* Niente ardesia (e' il grigio dei «dati assenti») ne' il tema. */
    static const int giro[] = {PH_DSK_ACC_INDACO, PH_DSK_ACC_CORALLO, PH_DSK_ACC_VERDE, PH_DSK_ACC_AMBRA,
                               PH_DSK_ACC_VIOLA};
    return giro[i % (sizeof giro / sizeof giro[0])];
}

const char *ph_dsk_accento_nome(int accento)
{
    static const char *const nomi[PH_DSK_ACC_N] = {"tema", "verde", "ambra", "corallo", "indaco", "viola", "ardesia"};
    return accento >= 0 && accento < PH_DSK_ACC_N ? nomi[accento] : nomi[PH_DSK_ACC_TEMA];
}

void ph_dsk_maiuscola_prima(const char *testo, char *out, size_t cap)
{
    if (cap == 0)
        return;
    out[0] = '\0';
    if (testo == NULL)
        return;
    if (testo[0] == '\0' || !g_utf8_validate(testo, -1, NULL)) {
        g_strlcpy(out, testo, cap);
        return;
    }
    gunichar c = g_utf8_get_char(testo);
    char prima[8];
    int k = g_unichar_to_utf8(g_unichar_toupper(c), prima);
    prima[k] = '\0';
    if ((size_t)k + 1 > cap) { /* nemmeno la prima lettera ci sta: si copia quanto entra com'e' */
        g_strlcpy(out, testo, cap);
        return;
    }
    g_strlcpy(out, prima, cap);
    g_strlcat(out, g_utf8_next_char(testo), cap);
}
