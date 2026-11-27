/* Prove di logica dei widget «vecchi» rifatti (logica_vecchi.h), SENZA schermo: che cosa si mostra
 * per ogni misura vera, fette della memoria, accenti, impegni in corso, maiuscola. */
#include <limits.h>
#include <stdio.h>
#include <string.h>

#include "logica_vecchi.h"

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

#define GB (1024ull * 1024 * 1024)

static void prova_utile(void)
{
    /* Le costanti della logica sono quelle vere della mattonella. */
    int o, v;
    ph_dsk_imbottitura(PH_DSK_FORMA_MATTONELLA, &o, &v);
    VERIFICA(o == PH_DSK_VECCHI_PAD_O && v == PH_DSK_VECCHI_PAD_V);
    int ul, ua;
    ph_dsk_vecchi_utile(250, 128, &ul, &ua);
    VERIFICA(ul == 210 && ua == 96);
    ph_dsk_vecchi_utile(-5, 10, &ul, &ua); /* mai negativo */
    VERIFICA(ul == 0 && ua == 0);
    ph_dsk_vecchi_utile(INT_MAX, INT_MAX, &ul, &ua); /* mai oltre il tetto */
    VERIFICA(ul == 100000 && ua == 100000);
    ph_dsk_vecchi_utile(10, 10, NULL, NULL); /* puntatori nulli ammessi */
}

static void prova_memoria_disp(void)
{
    /* Misura minima del tipo (200 x 120): dicitura sotto il numero, barra, nessuna legenda. */
    PhDskMemoriaDisp d = ph_dsk_memoria_disp(200, 120, 4, 1);
    VERIFICA(d.barra && !d.legenda && !d.nota_accanto);
    VERIFICA(d.numero_alto == 48); /* 88 utili - 16 (dicitura) - 24 (barra): il numero prende il resto */
    /* Poca altezza: niente barra, il numero prende tutto (fino al tetto). */
    d = ph_dsk_memoria_disp(200, 111, 0, 1); /* 79 utili: manca 1 px per la barra */
    VERIFICA(!d.barra && d.numero_alto == 62);
    d = ph_dsk_memoria_disp(200, 112, 0, 1); /* 80 utili: la barra ci sta */
    VERIFICA(d.barra && !d.legenda && d.numero_alto == 40);
    d = ph_dsk_memoria_disp(200, 60, 0, 1); /* non scende sotto il minimo del numero */
    VERIFICA(!d.barra && d.numero_alto == 40);
    /* La dicitura va accanto da 260 px utili (300 di larghezza), sotto prima. */
    VERIFICA(!ph_dsk_memoria_disp(299, 208, 0, 1).nota_accanto);
    VERIFICA(ph_dsk_memoria_disp(300, 208, 0, 1).nota_accanto);
    /* Larga e alta: legenda in due colonne con cinque voci, il numero al tetto. */
    d = ph_dsk_memoria_disp(320, 208, 5, 2);
    VERIFICA(d.barra && d.legenda && d.nota_accanto && d.numero_alto == 62);
    /* LA FASCIA DEL DIFETTO: tra 280 e ~305 di larghezza il componente puo' decidere una colonna
     * o due secondo la voce piu' larga vera. La logica usa le colonne che le arrivano: con una
     * colonna cinque voci non ci stanno (niente legenda), con due si'. */
    d = ph_dsk_memoria_disp(290, 208, 5, 1);
    VERIFICA(d.barra && !d.legenda);
    d = ph_dsk_memoria_disp(290, 208, 5, 2);
    VERIFICA(d.barra && d.legenda);
    /* Colonne non valide valgono una. */
    VERIFICA(ph_dsk_memoria_disp(290, 208, 5, 0).legenda == ph_dsk_memoria_disp(290, 208, 5, 1).legenda);
    VERIFICA(ph_dsk_memoria_disp(290, 208, 5, -3).legenda == ph_dsk_memoria_disp(290, 208, 5, 1).legenda);
    /* Voci in piu' di quelle che ci stanno: tutto o niente. */
    VERIFICA(ph_dsk_memoria_disp(320, 208, 8, 2).legenda);
    VERIFICA(!ph_dsk_memoria_disp(320, 208, 9, 2).legenda);
    d = ph_dsk_memoria_disp(320, 208, (size_t)-1, 2);
    VERIFICA(d.barra && !d.legenda);
    /* Misure assurde: niente barra, numero nei suoi limiti. */
    d = ph_dsk_memoria_disp(-1, -1, 3, 1);
    VERIFICA(!d.barra && !d.legenda && d.numero_alto == 40);
    d = ph_dsk_memoria_disp(INT_MAX, INT_MAX, 5, 2);
    VERIFICA(d.barra && d.legenda && d.numero_alto == 62);
}

static void prova_pacchetto_valido(void)
{
    VERIFICA(ph_dsk_pacchetto_valido("com.example.calendar"));
    VERIFICA(!ph_dsk_pacchetto_valido(NULL));
    VERIFICA(!ph_dsk_pacchetto_valido(""));
    char lungo[300];
    memset(lungo, 'a', sizeof lungo);
    lungo[255] = '\0';
    VERIFICA(ph_dsk_pacchetto_valido(lungo)); /* 255 byte: il massimo */
    lungo[256] = 'a';
    lungo[257] = '\0';
    lungo[255] = 'a';
    VERIFICA(!ph_dsk_pacchetto_valido(lungo)); /* 257 */
    VERIFICA(!ph_dsk_pacchetto_valido("com.\xff.x"));    /* UTF-8 non valido */
    VERIFICA(!ph_dsk_pacchetto_valido("com.a\nb"));      /* controllo */
    VERIFICA(!ph_dsk_pacchetto_valido("com.a\x7f" "b")); /* DEL */
    VERIFICA(!ph_dsk_pacchetto_valido("\x1f" "x"));
    VERIFICA(ph_dsk_pacchetto_valido("com.\xc3\xa8.x")); /* UTF-8 valido va bene */
}

static void prova_memoria_fette(void)
{
    PhSrvMemoriaDettaglio det = {{21 * GB, true}, {34 * GB, true}, {3 * GB, true}, {0, false}};
    PhDskMemFetta f[PH_DSK_MEM_N];
    /* Tre categorie lette e «Altro» = 80 - 58; l'app non letta non c'e' (e non vale zero). */
    size_t n = ph_dsk_memoria_fette(&det, 80 * GB, f);
    VERIFICA(n == 4);
    VERIFICA(f[0].categoria == PH_DSK_MEM_IMMAGINI && f[0].byte == 21 * GB);
    VERIFICA(f[1].categoria == PH_DSK_MEM_VIDEO && f[1].byte == 34 * GB);
    VERIFICA(f[2].categoria == PH_DSK_MEM_AUDIO && f[2].byte == 3 * GB);
    VERIFICA(f[3].categoria == PH_DSK_MEM_ALTRO && f[3].byte == 22 * GB);
    /* Le categorie spiegano tutto l'usato: niente «Altro». */
    n = ph_dsk_memoria_fette(&det, 58 * GB, f);
    VERIFICA(n == 3);
    /* Usato minore della somma (dato incoerente): niente «Altro» negativo. */
    n = ph_dsk_memoria_fette(&det, 10 * GB, f);
    VERIFICA(n == 3);
    /* Letta ma vuota: salta; le altre restano. */
    det.audio = (PhSrvVoceMemoria){0, true};
    n = ph_dsk_memoria_fette(&det, 80 * GB, f);
    VERIFICA(n == 3 && f[2].categoria == PH_DSK_MEM_ALTRO && f[2].byte == 25 * GB);
    /* Nessuna categoria letta: nessuna fetta (e nessun «Altro» che sarebbe tutto l'usato). */
    PhSrvMemoriaDettaglio nulla = {{0, false}, {0, false}, {0, false}, {0, false}};
    VERIFICA(ph_dsk_memoria_fette(&nulla, 80 * GB, f) == 0);
    VERIFICA(ph_dsk_memoria_fette(NULL, 80 * GB, f) == 0);
    VERIFICA(ph_dsk_memoria_fette(&det, 80 * GB, NULL) == 0);
    /* Letta ma tutta a zero: nessuna fetta, ma «Altro» e' tutto l'usato (il telefono ha detto 0). */
    PhSrvMemoriaDettaglio zero = {{0, true}, {0, true}, {0, true}, {0, true}};
    n = ph_dsk_memoria_fette(&zero, 5 * GB, f);
    VERIFICA(n == 1 && f[0].categoria == PH_DSK_MEM_ALTRO && f[0].byte == 5 * GB);
    /* Numeri enormi (dato ostile): niente trabocco della somma. */
    PhSrvMemoriaDettaglio enorme = {{UINT64_MAX, true}, {UINT64_MAX, true}, {0, false}, {0, false}};
    n = ph_dsk_memoria_fette(&enorme, UINT64_MAX, f);
    VERIFICA(n == 2); /* somma saturata a UINT64_MAX, usata non la supera */
    /* Colori: ogni categoria il suo, l'immagine con l'accento del tema. */
    VERIFICA(ph_dsk_memoria_accento(PH_DSK_MEM_IMMAGINI) == PH_DSK_ACC_TEMA);
    VERIFICA(ph_dsk_memoria_accento(PH_DSK_MEM_VIDEO) == PH_DSK_ACC_INDACO);
    VERIFICA(ph_dsk_memoria_accento(PH_DSK_MEM_AUDIO) == PH_DSK_ACC_AMBRA);
    VERIFICA(ph_dsk_memoria_accento(PH_DSK_MEM_APP) == PH_DSK_ACC_CORALLO);
    VERIFICA(ph_dsk_memoria_accento(PH_DSK_MEM_ALTRO) == PH_DSK_ACC_ARDESIA);
    VERIFICA(ph_dsk_memoria_accento((PhDskMemCategoria)99) == PH_DSK_ACC_ARDESIA);
    VERIFICA_STR(ph_dsk_pacchetto_impostazioni(), "com.android.settings");
}

/* Clic sui widget (27 nov): batteria -> pagina Batteria, memoria -> Archiviazione; tutti gli altri
 * tipi (anche «Stato del telefono» e la sveglia) restano senza pagina. I numeri sono quelli del
 * protocollo (1 batteria, 2 memoria): se cambiano qui cambia anche il telefono. */
static void prova_pagine(void)
{
    VERIFICA(ph_dsk_widget_pagina(PH_CFG_WIDGET_BATTERIA) == PH_SHELL_PAGINA_BATTERIA);
    VERIFICA(ph_dsk_widget_pagina(PH_CFG_WIDGET_BATTERIA) == 1);
    VERIFICA(ph_dsk_widget_pagina(PH_CFG_WIDGET_MEMORIA) == PH_SHELL_PAGINA_MEMORIA);
    VERIFICA(ph_dsk_widget_pagina(PH_CFG_WIDGET_MEMORIA) == 2);
    VERIFICA(ph_dsk_widget_pagina(PH_CFG_WIDGET_STATO) == PH_SHELL_PAGINA_NESSUNA);
    VERIFICA(ph_dsk_widget_pagina(PH_CFG_WIDGET_SVEGLIA) == PH_SHELL_PAGINA_NESSUNA);
    VERIFICA(ph_dsk_widget_pagina(PH_CFG_WIDGET_OROLOGIO) == PH_SHELL_PAGINA_NESSUNA);
    VERIFICA(ph_dsk_widget_pagina(PH_CFG_WIDGET_NOTIFICHE) == PH_SHELL_PAGINA_NESSUNA);
    VERIFICA(ph_dsk_widget_pagina(PH_CFG_WIDGET_CALENDARIO) == PH_SHELL_PAGINA_NESSUNA);
    VERIFICA(ph_dsk_widget_pagina((PhCfgWidgetTipo)999) == PH_SHELL_PAGINA_NESSUNA); /* fuori elenco */
}

static void prova_notifiche(void)
{
    /* Le misure equivalenti del tipo tengono 1 / 2 / 4 righe (specifica 5.2.3). */
    PhDskMisura mp = ph_dsk_misura_equivalente(PH_CFG_WIDGET_NOTIFICHE, 0, PH_CFG_WIDGET_PICCOLO);
    PhDskMisura mm = ph_dsk_misura_equivalente(PH_CFG_WIDGET_NOTIFICHE, 0, PH_CFG_WIDGET_MEDIO);
    PhDskMisura mg = ph_dsk_misura_equivalente(PH_CFG_WIDGET_NOTIFICHE, 0, PH_CFG_WIDGET_GRANDE);
    VERIFICA(ph_dsk_notifiche_righe_max(mp.larghezza, mp.altezza) == 1);
    VERIFICA(ph_dsk_notifiche_righe_max(mm.larghezza, mm.altezza) == 2);
    VERIFICA(ph_dsk_notifiche_righe_max(mg.larghezza, mg.altezza) == 4);
    VERIFICA(ph_dsk_notifiche_righe_max(200, 104) == 1);  /* minimo del tipo */
    VERIFICA(ph_dsk_notifiche_righe_max(250, 128) == 1);
    VERIFICA(ph_dsk_notifiche_righe_max(320, 168) == 2);
    VERIFICA(ph_dsk_notifiche_righe_max(320, 300) == 5);
    VERIFICA(ph_dsk_notifiche_righe_max(320, 299) == 5);
    VERIFICA(ph_dsk_notifiche_righe_max(320, 196) == 3); /* 164 utili: (164 - 22 + 2) / 46 = 3 */
    VERIFICA(ph_dsk_notifiche_righe_max(320, 190) == 3); /* il confine: 158 utili */
    VERIFICA(ph_dsk_notifiche_righe_max(320, 189) == 2);
    /* Mai zero (con meno spazio si taglia), anche con misure assurde; mai un trabocco. */
    VERIFICA(ph_dsk_notifiche_righe_max(0, 0) == 1);
    VERIFICA(ph_dsk_notifiche_righe_max(-10, -10) == 1);
    VERIFICA(ph_dsk_notifiche_righe_max(INT_MAX, INT_MAX) > 1000);
    /* Cresce con l'altezza, mai in calo. */
    size_t prec = 0;
    for (int a = 0; a < 700; a += 8) {
        size_t n = ph_dsk_notifiche_righe_max(320, a);
        VERIFICA(n >= prec);
        prec = n;
    }
}

static void prova_calendario(void)
{
    /* Misura minima: la data in una riga, un impegno. */
    PhDskCalDisp d = ph_dsk_calendario_disp(200, 110);
    VERIFICA(!d.colonna_giorno && d.max_impegni == 1);
    d = ph_dsk_calendario_disp(250, 150);
    VERIFICA(d.colonna_giorno && d.max_impegni == 2);
    d = ph_dsk_calendario_disp(320, 210);
    VERIFICA(d.colonna_giorno && d.max_impegni == 4);
    /* Il confine della colonna del giorno: 240 di larghezza (200 utili). */
    VERIFICA(ph_dsk_calendario_disp(240, 150).colonna_giorno);
    VERIFICA(!ph_dsk_calendario_disp(239, 150).colonna_giorno);
    /* Senza la colonna la riga della data toglie spazio: a pari altezza un impegno in meno. */
    VERIFICA(ph_dsk_calendario_disp(239, 172).max_impegni == 2 && ph_dsk_calendario_disp(240, 172).max_impegni == 3);
    /* Mai zero, nessun trabocco. */
    VERIFICA(ph_dsk_calendario_disp(-1, -1).max_impegni == 1);
    VERIFICA(ph_dsk_calendario_disp(INT_MAX, INT_MAX).max_impegni > 1000);
}

static void prova_impegno_in_corso(void)
{
    PhDskEvento e = {"x", 1000, 2000, false};
    VERIFICA(ph_dsk_impegno_in_corso(&e, 1000));  /* l'inizio e' dentro */
    VERIFICA(ph_dsk_impegno_in_corso(&e, 1999));
    VERIFICA(!ph_dsk_impegno_in_corso(&e, 2000)); /* la fine e' fuori */
    VERIFICA(!ph_dsk_impegno_in_corso(&e, 999));
    PhDskEvento giorno = {"x", 1000, 2000, true};
    VERIFICA(!ph_dsk_impegno_in_corso(&giorno, 1500)); /* tutto il giorno: non «ora» */
    PhDskEvento senza = {"x", 1000, 1000, false};
    VERIFICA(!ph_dsk_impegno_in_corso(&senza, 1000)); /* senza durata */
    VERIFICA(!ph_dsk_impegno_in_corso(NULL, 1000));
}

static void prova_stato(void)
{
    /* Le stesse righe di prima alle misure di prima (200x100, 250x124, 320x164). */
    VERIFICA(ph_dsk_stato_righe(200, 100) == ph_dsk_righe_stato(PH_CFG_WIDGET_PICCOLO));
    VERIFICA(ph_dsk_stato_righe(250, 124) == ph_dsk_righe_stato(PH_CFG_WIDGET_MEDIO));
    VERIFICA(ph_dsk_stato_righe(320, 164) == ph_dsk_righe_stato(PH_CFG_WIDGET_GRANDE));
    VERIFICA(ph_dsk_stato_righe(200, 100) == 0 && ph_dsk_stato_righe(250, 124) == 1 &&
             ph_dsk_stato_righe(320, 164) == 3);
    /* Mai piu' di tre (rete, pannello, schermo), mai negativo. */
    VERIFICA(ph_dsk_stato_righe(480, 640) == 3);
    VERIFICA(ph_dsk_stato_righe(0, 0) == 0);
    VERIFICA(ph_dsk_stato_righe(INT_MAX, INT_MAX) == 3);
    VERIFICA(ph_dsk_stato_righe(INT_MIN, INT_MIN) == 0);
    /* Una riga ogni 24 px: i confini. */
    VERIFICA(ph_dsk_stato_righe(250, 113) == 0 && ph_dsk_stato_righe(250, 114) == 1);
    VERIFICA(ph_dsk_stato_righe(250, 137) == 1 && ph_dsk_stato_righe(250, 138) == 2);
}

static void prova_accenti(void)
{
    /* Stesso pacchetto, stesso colore; mai quello del tema; vuoto = ardesia. */
    VERIFICA(ph_dsk_accento_pacchetto("com.example.chat") == ph_dsk_accento_pacchetto("com.example.chat"));
    VERIFICA(ph_dsk_accento_pacchetto(NULL) == PH_DSK_ACC_ARDESIA);
    VERIFICA(ph_dsk_accento_pacchetto("") == PH_DSK_ACC_ARDESIA);
    bool visti[PH_DSK_ACC_N] = {false};
    for (int i = 0; i < 400; i++) {
        char pk[48];
        snprintf(pk, sizeof pk, "com.prova.app%d", i);
        int a = ph_dsk_accento_pacchetto(pk);
        VERIFICA(a >= 1 && a < PH_DSK_ACC_N);
        if (a >= 0 && a < PH_DSK_ACC_N)
            visti[a] = true;
    }
    int quanti = 0;
    for (int a = 1; a < PH_DSK_ACC_N; a++)
        quanti += visti[a];
    VERIFICA(quanti >= 4); /* i colori si distribuiscono davvero */
    /* Gli impegni girano su cinque colori, senza tema e senza ardesia. */
    for (size_t i = 0; i < 200; i++) {
        int a = ph_dsk_accento_impegno(i);
        VERIFICA(a != PH_DSK_ACC_TEMA && a != PH_DSK_ACC_ARDESIA && a > 0 && a < PH_DSK_ACC_N);
    }
    VERIFICA(ph_dsk_accento_impegno(0) != ph_dsk_accento_impegno(1));
    VERIFICA(ph_dsk_accento_impegno(0) == ph_dsk_accento_impegno(5));
    VERIFICA(ph_dsk_accento_impegno((size_t)-1) > 0);
    /* I nomi sono quelli delle classi del CSS. */
    VERIFICA_STR(ph_dsk_accento_nome(PH_DSK_ACC_TEMA), "tema");
    VERIFICA_STR(ph_dsk_accento_nome(PH_DSK_ACC_CORALLO), "corallo");
    VERIFICA_STR(ph_dsk_accento_nome(PH_DSK_ACC_ARDESIA), "ardesia");
    VERIFICA_STR(ph_dsk_accento_nome(-1), "tema");
    VERIFICA_STR(ph_dsk_accento_nome(PH_DSK_ACC_N), "tema");
}

static void prova_maiuscola(void)
{
    char o[32];
    ph_dsk_maiuscola_prima("novembre", o, sizeof o);
    VERIFICA_STR(o, "Novembre");
    ph_dsk_maiuscola_prima("\xc3\xa8ra", o, sizeof o); /* «èra» -> «Èra» */
    VERIFICA_STR(o, "\xc3\x88ra");
    ph_dsk_maiuscola_prima("", o, sizeof o);
    VERIFICA_STR(o, "");
    ph_dsk_maiuscola_prima(NULL, o, sizeof o);
    VERIFICA_STR(o, "");
    ph_dsk_maiuscola_prima("ab\xff", o, sizeof o); /* non UTF-8: com'e' */
    VERIFICA_STR(o, "ab\xff");
    ph_dsk_maiuscola_prima("abcdefgh", o, 4); /* troncata al buffer, sempre terminata */
    VERIFICA_STR(o, "Abc");
    ph_dsk_maiuscola_prima("abc", o, 1);
    VERIFICA_STR(o, "");
    ph_dsk_maiuscola_prima("abc", NULL, 0); /* nessun buffer: niente da fare */
}

int main(void)
{
    prova_utile();
    prova_memoria_disp();
    prova_pacchetto_valido();
    prova_memoria_fette();
    prova_notifiche();
    prova_calendario();
    prova_impegno_in_corso();
    prova_stato();
    prova_accenti();
    prova_maiuscola();
    prova_pagine();
    if (errori == 0)
        printf("prova_vecchi: tutto bene\n");
    return errori == 0 ? 0 : 1;
}
