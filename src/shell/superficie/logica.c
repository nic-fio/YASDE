/* Logica pura della superficie: vedi logica.h. */
#include "logica.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "config/lingua.h"
#include "shell/servizi.h"

int ph_sup_byte_per_pixel(PhFotoFormato f)
{
    switch (f) {
    case PH_FOTO_B8G8R8A8_PREMOLT:
    case PH_FOTO_R8G8B8A8:
        return 4;
    case PH_FOTO_R8G8B8:
        return 3;
    }
    return 0;
}

int ph_sup_formato_gdk(PhFotoFormato f)
{
    switch (f) {
    case PH_FOTO_B8G8R8A8_PREMOLT:
        return GDK_MEMORY_B8G8R8A8_PREMULTIPLIED;
    case PH_FOTO_R8G8B8A8:
        return GDK_MEMORY_R8G8B8A8;
    case PH_FOTO_R8G8B8:
        return GDK_MEMORY_R8G8B8;
    }
    return -1;
}

bool ph_sup_controlla(const PhFotogramma *f)
{
    if (f == NULL)
        return false;
    if (f->larghezza < 1 || f->larghezza > PH_SUP_MISURA_MAX || f->altezza < 1 ||
        f->altezza > PH_SUP_MISURA_MAX)
        return false;
    if (f->tipo == PH_FOTO_MEMORIA) {
        int bpp = ph_sup_byte_per_pixel(f->formato);
        if (bpp == 0 || f->dati == NULL)
            return false;
        /* size_t: larghezza e bpp sono piccoli, nessun overflow. */
        size_t minimo = (size_t)f->larghezza * (size_t)bpp;
        return f->passo >= minimo && f->passo <= minimo + PH_SUP_PASSO_EXTRA &&
               f->passo <= SIZE_MAX / (size_t)f->altezza;
    }
    if (f->tipo == PH_FOTO_DMABUF) {
        if (f->piani < 1 || f->piani > PH_FOTO_PIANI_MAX || f->fourcc == 0)
            return false;
        for (int i = 0; i < f->piani; i++)
            if (f->fd[i] < 0 || f->passo_piano[i] == 0)
                return false;
        return true;
    }
    return false;
}

bool ph_sup_cicp(PhFotoColore c, unsigned *primari, unsigned *trasferimento, unsigned *matrice,
                 bool *gamma_ristretta)
{
    switch (c) {
    case PH_FOTO_COLORE_BT709_LIMITATO:
        *primari = 1; *trasferimento = 1; *matrice = 1; *gamma_ristretta = true;
        return true;
    case PH_FOTO_COLORE_BT601_LIMITATO:
        /* BT.601 (SMPTE 170M): primari 6, trasferimento 6, matrice 6. */
        *primari = 6; *trasferimento = 6; *matrice = 6; *gamma_ristretta = true;
        return true;
    case PH_FOTO_COLORE_BT709_PIENO:
        *primari = 1; *trasferimento = 1; *matrice = 1; *gamma_ristretta = false;
        return true;
    case PH_FOTO_COLORE_BT601_PIENO:
        *primari = 6; *trasferimento = 6; *matrice = 6; *gamma_ristretta = false;
        return true;
    case PH_FOTO_COLORE_PREDEFINITO:
        break;
    }
    return false;
}

void ph_sup_scambio_inizia(PhSupScambio *s)
{
    *s = (PhSupScambio){0};
    g_mutex_init(&s->m);
}

void ph_sup_scambio_libera(PhSupScambio *s)
{
    if (s->c_e)
        ph_fotogramma_rilascia(&s->attesa);
    s->c_e = false;
    g_mutex_clear(&s->m);
}

bool ph_sup_scambio_metti(PhSupScambio *s, PhFotogramma *fr, bool *sostituito)
{
    PhFotogramma vecchio = {0};
    bool manda = false;
    *sostituito = false;
    g_mutex_lock(&s->m);
    if (s->chiuso) {
        g_mutex_unlock(&s->m);
        ph_fotogramma_rilascia(fr);
        return false;
    }
    if (s->c_e) {
        vecchio = s->attesa;
        *sostituito = true;
    }
    s->attesa = *fr;
    *fr = (PhFotogramma){0};
    s->c_e = true;
    manda = !s->in_volo;
    s->in_volo = true;
    g_mutex_unlock(&s->m);
    /* Il rilascio (puo' chiudere descrittori) fuori dal lucchetto. */
    ph_fotogramma_rilascia(&vecchio);
    return manda;
}

bool ph_sup_scambio_prendi(PhSupScambio *s, PhFotogramma *out)
{
    g_mutex_lock(&s->m);
    bool c = s->c_e;
    if (c)
        *out = s->attesa;
    s->attesa = (PhFotogramma){0};
    s->c_e = false;
    s->in_volo = false;
    g_mutex_unlock(&s->m);
    return c;
}

void ph_sup_scambio_chiudi(PhSupScambio *s)
{
    PhFotogramma f = {0};
    g_mutex_lock(&s->m);
    s->chiuso = true;
    if (s->c_e)
        f = s->attesa;
    s->attesa = (PhFotogramma){0};
    s->c_e = false;
    g_mutex_unlock(&s->m);
    ph_fotogramma_rilascia(&f);
}

void ph_sup_rettangolo(int area_l, int area_a, int video_l, int video_a, bool a_riposo, int *x, int *y,
                       int *l, int *a)
{
    *x = 0;
    *y = 0;
    *l = area_l;
    *a = area_a;
    if (!a_riposo || area_l <= 0 || area_a <= 0 || video_l <= 0 || video_a <= 0)
        return;
    /* Confronto per prodotto in 64 bit: niente divisioni, niente overflow. */
    long long area_per_video = (long long)area_l * video_a; /* l_area/a_area vs l_video/a_video */
    long long video_per_area = (long long)video_l * area_a;
    if (area_per_video == video_per_area)
        return;
    if (area_per_video > video_per_area) {
        /* L'area e' piu' larga del video: bande ai lati. */
        *l = (int)(((long long)area_a * video_l + video_a / 2) / video_a);
        if (*l < 1)
            *l = 1;
        if (*l > area_l)
            *l = area_l;
        *x = (area_l - *l) / 2;
    } else {
        /* L'area e' piu' alta: bande sopra e sotto. */
        *a = (int)(((long long)area_l * video_a + video_l / 2) / video_l);
        if (*a < 1)
            *a = 1;
        if (*a > area_a)
            *a = area_a;
        *y = (area_a - *a) / 2;
    }
}

void ph_sup_sforzo_nota(PhSupSforzo *s, gint64 ora_us, bool sostituito, bool primo_piano,
                        bool *cambiato, unsigned *causa, bool *per_primo_piano)
{
    *cambiato = false;
    *causa = 0;
    *per_primo_piano = false;
    if (primo_piano && s->livello != 0) {
        /* Passata in primo piano: non rinuncia mai per scelta propria. */
        *causa = s->sostituiti;
        s->livello = 0;
        s->puliti = 0;
        s->ricadute = 0;
        s->livello_scritto = 0;
        s->sostituiti = 0;
        s->inizio = ora_us;
        s->ultimo_log = ora_us;
        *cambiato = true;
        *per_primo_piano = true;
        return;
    }
    if (s->inizio == 0)
        s->inizio = ora_us;
    if (sostituito)
        s->sostituiti++;
    if (ora_us - s->inizio < PH_SUP_FINESTRA_US)
        return;
    unsigned n = s->sostituiti;
    int vecchio = s->livello;
    int richieste = 2 << (s->ricadute > 3 ? 3 : s->ricadute);
    if (richieste > PH_SUP_PULITE_MAX)
        richieste = PH_SUP_PULITE_MAX;
    if (!primo_piano && n >= PH_SUP_SOGLIA_SOSTITUITI && s->livello < PH_SUP_LIVELLO_MAX) {
        if (s->livello == 0 && s->tornato)
            s->ricadute++; /* ricaduta: la prossima volta serve piu' calma per tornare */
        s->livello++;
    } else if (n == 0 && s->livello > 0 && ++s->puliti >= richieste) {
        s->livello = 0;
        s->tornato = true;
    }
    if (n != 0 || s->livello == 0)
        s->puliti = 0;
    (void)vecchio;
    /* Si scrive lo stato VERO, confrontandolo con l'ultimo livello scritto: un cambio
     * caduto nei 3 s di silenzio del registro si recupera alla chiusura di una finestra successiva. */
    /* Ogni RIDUZIONE si scrive sempre (4.12); il limite dei 3 s vale solo per i ritorni. */
    bool salita = s->livello > s->livello_scritto;
    if (s->livello != s->livello_scritto &&
        (salita || s->ultimo_log == 0 || ora_us - s->ultimo_log >= PH_SUP_LOG_MIN_US)) {
        s->ultimo_log = ora_us;
        s->livello_scritto = s->livello;
        *cambiato = true;
        *causa = n;
    }
    s->inizio = ora_us;
    s->sostituiti = 0;
}

bool ph_sup_sforzo_salta(PhSupSforzo *s)
{
    unsigned n = s->contatore++;
    if (s->livello <= 0)
        return false;
    return (n & ((1u << s->livello) - 1u)) != 0;
}

int ph_sup_satura_i16(int v)
{
    return v > 32767 ? 32767 : (v < -32768 ? -32768 : v);
}

PhSupTenuto *ph_sup_tenuto_nuovo(PhFotogramma *fr, int riferimenti)
{
    PhSupTenuto *t = g_new0(PhSupTenuto, 1);
    t->f = *fr;
    *fr = (PhFotogramma){0};
    atomic_init(&t->rif, riferimenti);
    atomic_init(&t->gtk_chiamato, false);
    return t;
}

void ph_sup_tenuto_lascia(PhSupTenuto *t)
{
    if (atomic_fetch_sub(&t->rif, 1) != 1)
        return;
    ph_fotogramma_rilascia(&t->f);
    g_free(t);
}

void ph_sup_tenuto_da_gtk(gpointer p)
{
    PhSupTenuto *t = p;
    atomic_store(&t->gtk_chiamato, true);
    ph_sup_tenuto_lascia(t);
}

void ph_sup_tenuto_costruito(PhSupTenuto *t, bool riuscita)
{
    if (!riuscita && !atomic_load(&t->gtk_chiamato))
        ph_sup_tenuto_lascia(t); /* il riferimento di GTK non e' mai stato usato */
    ph_sup_tenuto_lascia(t);     /* quello di chi costruisce */
}

bool ph_sup_ritmo_arriva(PhSupRitmo *r, PhFotogramma *fr, gint64 ora_us, bool primo_piano, bool *cambiato,
                         unsigned *causa, bool *per_primo_piano)
{
    *cambiato = false;
    *causa = 0;
    *per_primo_piano = false;
    if (primo_piano && r->sf.livello != 0)
        ph_sup_sforzo_nota(&r->sf, ora_us, false, true, cambiato, causa, per_primo_piano);
    if (ph_sup_sforzo_salta(&r->sf)) {
        if (r->c_e)
            ph_fotogramma_rilascia(&r->da_parte); /* il precedente da parte e' superato */
        r->da_parte = *fr;
        *fr = (PhFotogramma){0};
        r->c_e = true;
        return false;
    }
    if (r->c_e) {
        ph_fotogramma_rilascia(&r->da_parte); /* ne passa uno piu' nuovo */
        r->c_e = false;
    }
    return true;
}

void ph_sup_ritmo_dopo_scambio(PhSupRitmo *r, gint64 ora_us, bool sostituito, bool primo_piano,
                               bool *cambiato, unsigned *causa, bool *per_primo_piano)
{
    ph_sup_sforzo_nota(&r->sf, ora_us, sostituito, primo_piano, cambiato, causa, per_primo_piano);
}

bool ph_sup_ritmo_silenzio(PhSupRitmo *r, PhFotogramma *out, gint64 ora_us, bool primo_piano,
                           bool *cambiato, unsigned *causa, bool *per_primo_piano)
{
    ph_sup_sforzo_nota(&r->sf, ora_us, false, primo_piano, cambiato, causa, per_primo_piano);
    if (!r->c_e)
        return false;
    *out = r->da_parte;
    r->da_parte = (PhFotogramma){0};
    r->c_e = false;
    return true;
}

void ph_sup_ritmo_libera(PhSupRitmo *r)
{
    if (r->c_e)
        ph_fotogramma_rilascia(&r->da_parte);
    r->c_e = false;
}

bool ph_sup_decodifica_png(GBytes *png, GdkTexture **immagine, char **frase)
{
    GError *err = NULL;
    *immagine = NULL;
    *frase = NULL;
    GdkTexture *t = png != NULL ? gdk_texture_new_from_bytes(png, &err) : NULL;
    if (t == NULL) {
        fprintf(stderr, "superficie: PNG dello screenshot non decodificabile: %s\n",
                err != NULL ? err->message : "nessun dato");
        g_clear_error(&err);
        *frase = g_strdup(ph_t("Non riesco a leggere l'immagine dello screenshot."));
        return false;
    }
    *immagine = t;
    return true;
}

bool ph_sup_chiave_da_chiedere(bool era_protetto, bool ora_protetto)
{
    return era_protetto && !ora_protetto;
}

PhSupEsitoScatto ph_sup_esito_scatto(bool esito_servizi_ok, bool file_salvato, PhSupDecodifica decodifica)
{
    PhSupEsitoScatto r = {false, false, false};
    if (!esito_servizi_ok)
        return r; /* errore dei servizi: nessuna immagine, la frase e' la loro */
    switch (decodifica) {
    case PH_SUP_DEC_NON_CHIESTA:
        r.esito_ok = true;
        break;
    case PH_SUP_DEC_RIUSCITA:
        r.esito_ok = true;
        r.immagine = true;
        break;
    case PH_SUP_DEC_FALLITA:
    case PH_SUP_DEC_NO_THREAD:
        /* Il file salvato resta una buona notizia; solo per gli appunti e' un errore. */
        r.esito_ok = file_salvato;
        r.frase_nostra = true;
        break;
    }
    return r;
}

PhSupRett ph_sup_rettangolo_esatto(int area_l, int area_a, double es_l, double es_a, int video_l, int video_a,
                                   double scala, bool a_riposo)
{
    PhSupRett r;
    if (!a_riposo || es_l <= 0 || es_a <= 0 || scala <= 0) {
        int x, y, l, a;
        ph_sup_rettangolo(area_l, area_a, video_l, video_a, a_riposo, &x, &y, &l, &a);
        r = (PhSupRett){x, y, l, a};
        return r;
    }
    /* La misura esatta non esce mai dall'area allocata. */
    if (es_l > area_l)
        es_l = area_l;
    if (es_a > area_a)
        es_a = area_a;
    r = (PhSupRett){0, 0, es_l, es_a};
    if (video_l <= 0 || video_a <= 0)
        return r;
    double dl = (double)video_l - es_l * scala, da = (double)video_a - es_a * scala;
    if (dl <= 0.5 && dl >= -0.5 && da <= 0.5 && da >= -0.5)
        return r; /* 1:1: esattamente il rettangolo */
    /* Proporzioni diverse: dentro il rettangolo, centrata, con bande. */
    if (es_l * video_a > es_a * video_l) {
        r.l = es_a * video_l / video_a;
        r.x = (es_l - r.l) / 2;
    } else {
        r.a = es_l * video_a / video_l;
        r.y = (es_a - r.a) / 2;
    }
    return r;
}

bool ph_sup_pizzico_ammesso(int fase, bool in_pausa)
{
    if (fase < PH_SRV_PIZZICO_INIZIO || fase > PH_SRV_PIZZICO_ANNULLA)
        return false;
    return !in_pausa;
}
