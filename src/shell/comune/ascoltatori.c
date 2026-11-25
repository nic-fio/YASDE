/* Elenco di ascoltatori (interno.h). */
#include "interno.h"

void ph_ascoltatori_inizia(PhAscoltatori *a)
{
    a->voci = g_array_new(FALSE, TRUE, sizeof(PhAscoltatore));
    a->prossimo = 1; /* non piu' usato: gli id sono unici nel processo */
    a->in_corso = 0;
}

void ph_ascoltatori_finisci(PhAscoltatori *a)
{
    if (a->voci != NULL)
        g_array_free(a->voci, TRUE);
    a->voci = NULL;
}

guint ph_ascoltatori_aggiungi(PhAscoltatori *a, int evento, PhFnGenerica fn, void *utente)
{
    if (fn == NULL)
        return 0;
    /* Id unici nel PROCESSO (mai riusati, mai 0): un id di un oggetto liberato non
     * puo' coincidere con uno di un oggetto nuovo nato allo stesso indirizzo. */
    static gint prossimo_globale;
    guint id = (guint)g_atomic_int_add(&prossimo_globale, 1) + 1;
    if (id == 0)
        id = (guint)g_atomic_int_add(&prossimo_globale, 1) + 1;
    PhAscoltatore v = {id, evento, fn, utente, false};
    g_array_append_val(a->voci, v);
    return v.id;
}

static void compatta(PhAscoltatori *a)
{
    for (guint i = 0; i < a->voci->len;) {
        if (g_array_index(a->voci, PhAscoltatore, i).tolto)
            g_array_remove_index(a->voci, i);
        else
            i++;
    }
}

void ph_ascoltatori_togli(PhAscoltatori *a, guint id)
{
    for (guint i = 0; i < a->voci->len; i++) {
        PhAscoltatore *v = &g_array_index(a->voci, PhAscoltatore, i);
        if (v->id == id)
            v->tolto = true;
    }
    if (a->in_corso == 0)
        compatta(a);
}

guint ph_ascoltatori_apri(PhAscoltatori *a)
{
    a->in_corso++;
    return a->voci->len;
}

const PhAscoltatore *ph_ascoltatori_voce(PhAscoltatori *a, guint i, int evento)
{
    if (i >= a->voci->len)
        return NULL;
    const PhAscoltatore *v = &g_array_index(a->voci, PhAscoltatore, i);
    return (v->tolto || v->evento != evento) ? NULL : v;
}

void ph_ascoltatori_chiudi(PhAscoltatori *a)
{
    if (--a->in_corso == 0)
        compatta(a);
}

/* ---- Registro degli oggetti vivi ------------------------------------------ */
static GMutex vivi_m;
static GHashTable *vivi;

void ph_vivi_aggiungi(const void *p)
{
    g_mutex_lock(&vivi_m);
    if (vivi == NULL)
        vivi = g_hash_table_new(g_direct_hash, g_direct_equal);
    g_hash_table_add(vivi, (gpointer)p);
    g_mutex_unlock(&vivi_m);
}

void ph_vivi_togli(const void *p)
{
    g_mutex_lock(&vivi_m);
    if (vivi != NULL)
        g_hash_table_remove(vivi, p);
    g_mutex_unlock(&vivi_m);
}

bool ph_vivi_e(const void *p)
{
    g_mutex_lock(&vivi_m);
    bool v = vivi != NULL && p != NULL && g_hash_table_contains(vivi, p);
    g_mutex_unlock(&vivi_m);
    return v;
}
