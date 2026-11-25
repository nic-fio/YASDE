/* Servizi veri: audio (4.9) e volume lato PC (5.8). L'audio parte
 * SV_AUDIO_RITARDO_MS dopo l'apertura del primo flusso video della sessione
 * (proposta del COMPITO, da misurare: la regola della specifica "dopo lo specchio e 5 s
 * dopo" qui non ha lo specchio all'avvio). Tutta questa politica sta in
 * sv_audio_programma. Il muto non ferma la lettura (audio.h). */
#include <stdio.h>

#include "sv_interno.h"

#if PH_OSSATURA_SERVIZI_VERI

#define SV_AUDIO_RITARDO_MS 5000

#define SV_AUDIO_TENTATIVI 4

/* Programma una riapertura con intervallo crescente (2, 4, 8, 16 s), poche
 * volte per sessione. Chiamata con sv->am preso; torna true se l'ha programmata. */
static bool riprova_programma(Sv *sv)
{
    if (atomic_load(&sv->chiusura) || sv->audio_tentativi >= SV_AUDIO_TENTATIVI)
        return false;
    sv->audio_scadenza_us = g_get_monotonic_time() + ((gint64)2000000 << sv->audio_tentativi);
    sv->audio_tentativi++;
    return true;
}

/* Dal thread dell'audio (o di PipeWire): niente servizi, niente distruggi
 * (non si puo' dalla funzione di avviso): si programma la riapertura e la fa
 * il gestore. */
static void su_audio_finito(void *ctx, int esito, const char *testo)
{
    (void)testo;
    Sv *sv = ctx;
    g_mutex_lock(&sv->am);
    bool riprova = sv->audio != NULL && riprova_programma(sv);
    g_mutex_unlock(&sv->am);
    ph_shell_decisione("audio fermato", "%s%s", esito == PH_AUDIO_FINITO ? "il telefono ha chiuso il canale"
                                                                          : "errore del canale o dell'uscita audio",
                       riprova ? ": lo riapro fra poco" : "");
    if (riprova)
        sv_manager_invia(sv, SV_MG_AUDIO);
}

static void volume_pubblica(Sv *sv, PhSrvVolume v)
{
    ph_servizi_pubblica_volume(sv->serv, &v);
}

void sv_volume(void *b, int volume)
{
    Sv *sv = b;
    if (volume < 0)
        volume = 0;
    if (volume > 100)
        volume = 100;
    g_mutex_lock(&sv->am);
    sv->vol.volume = volume;
    if (sv->audio != NULL)
        ph_audio_volume(sv->audio, volume); /* non blocca */
    PhSrvVolume v = sv->vol;
    g_mutex_unlock(&sv->am);
    volume_pubblica(sv, v);
}

void sv_muto(void *b, bool muto)
{
    Sv *sv = b;
    g_mutex_lock(&sv->am);
    sv->vol.muto = muto;
    if (sv->audio != NULL)
        ph_audio_muto(sv->audio, muto);
    PhSrvVolume v = sv->vol;
    g_mutex_unlock(&sv->am);
    volume_pubblica(sv, v);
}

void sv_audio_volume_iniziale(Sv *sv, int volume)
{
    g_mutex_lock(&sv->am);
    sv->vol.volume = volume < 0 ? 0 : (volume > 100 ? 100 : volume);
    if (sv->audio != NULL)
        ph_audio_volume(sv->audio, sv->vol.volume);
    PhSrvVolume v = sv->vol;
    g_mutex_unlock(&sv->am);
    volume_pubblica(sv, v);
}

void sv_audio_programma(Sv *sv)
{
    bool nuovo = false;
    g_mutex_lock(&sv->am);
    if (!sv->audio_avviato && sv->audio == NULL && sv->audio_scadenza_us == 0) {
        sv->audio_scadenza_us = g_get_monotonic_time() + SV_AUDIO_RITARDO_MS * 1000;
        nuovo = true;
    }
    g_mutex_unlock(&sv->am);
    if (nuovo)
        sv_manager_invia(sv, SV_MG_AUDIO); /* sveglia il gestore: ricalcola l'attesa */
}

gint64 sv_audio_attesa_us(Sv *sv)
{
    g_mutex_lock(&sv->am);
    gint64 sc = sv->audio_scadenza_us;
    g_mutex_unlock(&sv->am);
    if (sc == 0)
        return -1;
    gint64 r = sc - g_get_monotonic_time();
    return r < 0 ? 0 : r;
}

void sv_audio_avvia_se_dovuto(Sv *sv)
{
    g_mutex_lock(&sv->am);
    gint64 sc = sv->audio_scadenza_us;
    bool dovuto = sc != 0 && g_get_monotonic_time() >= sc;
    if (dovuto)
        sv->audio_scadenza_us = 0;
    g_mutex_unlock(&sv->am);
    if (!dovuto || atomic_load(&sv->chiusura))
        return;
    SvSessione *s = sv_sessione_usa(sv);
    if (s == NULL)
        return;
    /* Un audio fermato nella stessa sessione si toglie prima di riaprirlo. */
    g_mutex_lock(&sv->am);
    PhAudio *vecchio = sv->audio;
    sv->audio = NULL;
    g_mutex_unlock(&sv->am);
    if (vecchio != NULL)
        ph_audio_distruggi(vecchio);
    PhErrore e = {{0}};
    PhCanale *canale = ph_componente_canale(s->c, "audio", &e);
    PhAudio *a = canale != NULL ? ph_audio_crea(canale, su_audio_finito, sv, &e) : NULL; /* il canale passa a lui */
    if (a != NULL && ph_audio_avvia(a, &e) != 0) {
        ph_audio_distruggi(a);
        a = NULL;
    }
    if (a == NULL) {
        sv_sessione_lascia(s);
        g_mutex_lock(&sv->am);
        bool riprova = riprova_programma(sv);
        g_mutex_unlock(&sv->am);
        ph_shell_decisione("audio non avviato", "il canale o l'uscita audio non si sono aperti%s",
                           riprova ? ": riprovo fra poco" : "");
        return;
    }
    g_mutex_lock(&sv->am);
    ph_audio_volume(a, sv->vol.volume);
    ph_audio_muto(a, sv->vol.muto);
    sv->audio = a;
    sv->audio_avviato = true;
    g_mutex_unlock(&sv->am);
    sv_sessione_lascia(s);
    ph_shell_decisione("audio avviato", "%d s dopo il primo flusso video", SV_AUDIO_RITARDO_MS / 1000);
}

void sv_audio_teardown(Sv *sv)
{
    g_mutex_lock(&sv->am);
    PhAudio *a = sv->audio;
    sv->audio = NULL;
    sv->audio_avviato = false;
    sv->audio_scadenza_us = 0;
    sv->audio_tentativi = 0;
    g_mutex_unlock(&sv->am);
    /* Ferma, chiude il canale e libera: fuori dal lucchetto (non va chiamata
     * insieme ad altre funzioni dello stesso PhAudio: le altre le fa
     * sv_volume/sv_muto sotto lucchetto e vedono gia' NULL). */
    if (a != NULL)
        ph_audio_distruggi(a);
}

#endif
