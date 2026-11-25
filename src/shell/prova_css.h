/* Rilevatore comune dei colori scritti a mano nei fogli di stile dei pezzi
 * della shell. Lo usano le prove di ogni pezzo (stesso codice per tutti: prima
 * c'erano sette versioni, ognuna aggirabile in modo diverso).
 *
 * Perche': il CSS di un pezzo non deve scrivere colori di contenuto, perche' lo
 * stesso widget sta su vetro chiaro o scuro: i colori vengono dalle variabili
 * del tema (var(--ph-...)). Un colore che serve davvero cosi' com'e' (la tinta
 * di un'icona, un'ombra) si marca con l'eccezione.
 *
 * REGOLA. Si tolgono i commenti CSS (anche su piu' righe: le righe restano al
 * loro posto, cosi' i numeri di riga sono quelli del file), poi si cerca, nei
 * VALORI delle dichiarazioni (dopo ':' dentro un blocco { }), e in ogni punto
 * per @define-color, uno di questi:
 *   - '#' seguito da 3-8 cifre esadecimali (e poi un carattere che non fa parte
 *     di un nome: '#abcxyz' non e' un colore);
 *   - le funzioni rgb( rgba( hsl( hsla( hwb( lab( oklab( lch( oklch( color(
 *     color-mix( alpha( shade( mix( lighter( darker(;
 *   - '@nome' usato come valore (colore con nome del tema di GTK) e
 *     @define-color;
 *   - i colori con nome di CSS usati come valore (i 148 nomi: white, red,
 *     whitesmoke, darkgray, crimson...). NON sono colori scritti a mano:
 *     transparent, currentColor, inherit, none, var(...), initial, unset.
 * I selettori non contano ('#id' prima della ':' del valore, o fuori da un
 * blocco): un selettore '#add' non e' un colore. Il contenuto delle stringhe si
 * ignora.
 *
 * ECCEZIONE. Il commento `/ * colore-dato: motivo * /` (senza gli spazi dentro
 * le sbarre) sulla STESSA riga del colore. Va riconosciuto prima di togliere i
 * commenti; il motivo e' obbligatorio (un `colore-dato:` vuoto e' una
 * violazione a se'). La riga con l'eccezione deve avere UNA sola dichiarazione
 * (al massimo un ';' nel codice e una sola dichiarazione che inizia sulla riga,
 * anche se contiene piu' colori): con due dichiarazioni non si sa a quale
 * vale, quindi e' una violazione. Una dichiarazione spezzata su piu' righe
 * porta l'eccezione sulla riga in cui sta il colore. Un commento di eccezione
 * su un'altra riga non vale.
 *
 * LIMITI NOTI. Dentro un blocco annidato (per esempio in @media) il ':' di un
 * selettore come `a:not(.red)` fa leggere `red` come valore: falso allarme
 * (si riscrive il selettore o si sposta fuori dal blocco). Non si guarda dentro
 * url(...). L'eccezione scusa una sola dichiarazione per riga (con piu' colori
 * dentro, come un gradiente, va bene): se sulla riga ne iniziano due, anche con
 * un solo ';', nessun colore della riga e' scusato.
 * QUESTO FILE NON E' UN CONTRATTO: serve solo alle prove dei pezzi e lo
 * possono modificare le prove (regole e liste comprese).
 *
 * Il valore di ritorno e' il numero di violazioni; ognuna si stampa su stderr
 * come `nome:riga: che cosa: testo della riga` (se stampa e' true). */
#ifndef PH_SHELL_PROVA_CSS_H
#define PH_SHELL_PROVA_CSS_H

#include <gio/gio.h>
#include <glib.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

static inline bool ph_prova_css_e_ident(char c)
{
    return g_ascii_isalnum(c) || c == '_' || c == '-';
}

static inline bool ph_prova_css_in_lista(const char *s, size_t n, const char *const *lista)
{
    for (; *lista != NULL; lista++)
        if (strlen(*lista) == n && g_ascii_strncasecmp(s, *lista, n) == 0)
            return true;
    return false;
}

static inline bool ph_prova_css_funzione_colore(const char *s, size_t n)
{
    static const char *const f[] = {"rgb",  "rgba",  "hsl",   "hsla",  "hwb",     "lab",
                                    "oklab", "lch",  "oklch", "color", "color-mix", "alpha",
                                    "shade", "mix",  "lighter", "darker", NULL};
    return ph_prova_css_in_lista(s, n, f);
}

static inline bool ph_prova_css_nome_colore(const char *s, size_t n)
{
    static const char *const c[] = {
    "aliceblue", "antiquewhite", "aqua", "aquamarine", "azure", "beige", "bisque", "black",
    "blanchedalmond", "blue", "blueviolet", "brown", "burlywood", "cadetblue", "chartreuse",
    "chocolate", "coral", "cornflowerblue", "cornsilk", "crimson", "cyan", "darkblue",
    "darkcyan", "darkgoldenrod", "darkgray", "darkgreen", "darkgrey", "darkkhaki",
    "darkmagenta", "darkolivegreen", "darkorange", "darkorchid", "darkred", "darksalmon",
    "darkseagreen", "darkslateblue", "darkslategray", "darkslategrey", "darkturquoise",
    "darkviolet", "deeppink", "deepskyblue", "dimgray", "dimgrey", "dodgerblue", "firebrick",
    "floralwhite", "forestgreen", "fuchsia", "gainsboro", "ghostwhite", "gold", "goldenrod",
    "gray", "green", "greenyellow", "grey", "honeydew", "hotpink", "indianred", "indigo",
    "ivory", "khaki", "lavender", "lavenderblush", "lawngreen", "lemonchiffon", "lightblue",
    "lightcoral", "lightcyan", "lightgoldenrodyellow", "lightgray", "lightgreen", "lightgrey",
    "lightpink", "lightsalmon", "lightseagreen", "lightskyblue", "lightslategray",
    "lightslategrey", "lightsteelblue", "lightyellow", "lime", "limegreen", "linen", "magenta",
    "maroon", "mediumaquamarine", "mediumblue", "mediumorchid", "mediumpurple",
    "mediumseagreen", "mediumslateblue", "mediumspringgreen", "mediumturquoise",
    "mediumvioletred", "midnightblue", "mintcream", "mistyrose", "moccasin", "navajowhite",
    "navy", "oldlace", "olive", "olivedrab", "orange", "orangered", "orchid", "palegoldenrod",
    "palegreen", "paleturquoise", "palevioletred", "papayawhip", "peachpuff", "peru", "pink",
    "plum", "powderblue", "purple", "rebeccapurple", "red", "rosybrown", "royalblue",
    "saddlebrown", "salmon", "sandybrown", "seagreen", "seashell", "sienna", "silver",
    "skyblue", "slateblue", "slategray", "slategrey", "snow", "springgreen", "steelblue",
    "tan", "teal", "thistle", "tomato", "turquoise", "violet", "wheat", "white", "whitesmoke",
    "yellow", "yellowgreen", NULL};
    return ph_prova_css_in_lista(s, n, c);
}

static inline int ph_prova_css_colori_a_mano(const char *css, const char *nome, bool stampa)
{
    size_t n = strlen(css);
    int righe = 1;
    for (size_t i = 0; i < n; i++)
        if (css[i] == '\n')
            righe++;
    char *codice = g_malloc(n + 1);
    char *marca = g_new0(char, (gsize)righe + 2); /* per riga: 0 nessuna, 1 eccezione col motivo, 2 senza motivo */
    int *semi = g_new0(int, (gsize)righe + 2);    /* ';' nel codice di ogni riga */
    const char **inizio = g_new0(const char *, (gsize)righe + 2);
    int *dich_riga = g_new0(int, (gsize)righe + 2); /* dichiarazioni che iniziano su ogni riga */
    typedef struct {
        int riga;
        const char *che;
    } Trovato;
    GArray *trovati = g_array_new(FALSE, FALSE, sizeof(Trovato));
    int viol = 0;

    inizio[1] = css;
    int r = 1;
    for (size_t i = 0; i < n; i++)
        if (css[i] == '\n' && r + 1 <= righe)
            inizio[++r] = css + i + 1;

    /* 1) Eccezioni, poi commenti tolti (a spazi; gli a capo restano). */
    size_t i = 0;
    r = 1;
    while (i < n) {
        if (css[i] == '/' && i + 1 < n && css[i + 1] == '*') {
            const char *fine = strstr(css + i + 2, "*/");
            size_t j = fine != NULL ? (size_t)(fine - css) : n;
            for (size_t k = i + 2; k + 12 <= j; k++) {
                if (strncmp(css + k, "colore-dato:", 12) == 0) {
                    int rk = r;
                    for (size_t q = i; q < k; q++)
                        if (css[q] == '\n')
                            rk++;
                    size_t m = k + 12;
                    while (m < j && g_ascii_isspace(css[m]))
                        m++;
                    marca[rk] = m < j ? 1 : 2;
                    break;
                }
            }
            size_t fin = fine != NULL ? j + 2 : n;
            for (; i < fin; i++) {
                codice[i] = css[i] == '\n' ? '\n' : ' ';
                if (css[i] == '\n')
                    r++;
            }
        } else {
            codice[i] = css[i];
            if (css[i] == '\n')
                r++;
            i++;
        }
    }
    codice[n] = '\0';
    r = 1;
    for (i = 0; i < n; i++) {
        if (codice[i] == '\n')
            r++;
        else if (codice[i] == ';')
            semi[r]++;
    }
    for (int k = 1; k <= righe; k++) {
        if (marca[k] == 2) {
            viol++;
            if (stampa) {
                const char *e = strchr(inizio[k], '\n');
                fprintf(stderr, "%s:%d: eccezione colore-dato senza motivo: %.*s\n", nome, k,
                        (int)(e != NULL ? (size_t)(e - inizio[k]) : strlen(inizio[k])), inizio[k]);
            }
        }
    }

    /* 2) Ricerca nel codice. */
    int prof = 0;
    bool valore = false;
    r = 1;
    i = 0;
    while (i < n) {
        char c = codice[i];
        const char *che = NULL;
        size_t avanza = 1;
        if (c == '\n') {
            r++;
        } else if (c == '"' || c == '\'') {
            size_t j = i + 1;
            while (j < n && codice[j] != c) {
                if (codice[j] == '\n')
                    r++;
                j++;
            }
            avanza = j + 1 - i;
        } else if (c == '{') {
            prof++;
            valore = false;
        } else if (c == '}') {
            if (prof > 0)
                prof--;
            valore = false;
        } else if (c == ';') {
            valore = false;
        } else if (c == ':') {
            if (prof > 0) {
                if (!valore)
                    dich_riga[r]++;
                valore = true;
            }
        } else if (c == '@') {
            size_t j = i + 1;
            while (j < n && ph_prova_css_e_ident(codice[j]))
                j++;
            if (j - i - 1 == 12 && strncmp(codice + i + 1, "define-color", 12) == 0)
                che = "@define-color";
            else if (valore && j > i + 1)
                che = "@nome usato come colore";
            avanza = j > i + 1 ? j - i : 1;
        } else if (c == '#') {
            size_t j = i + 1;
            while (j < n && g_ascii_isxdigit(codice[j]))
                j++;
            size_t h = j - i - 1;
            if (valore && h >= 3 && h <= 8 && (j >= n || !ph_prova_css_e_ident(codice[j])))
                che = "colore esadecimale";
            avanza = j > i ? j - i : 1;
        } else if (ph_prova_css_e_ident(c)) {
            size_t j = i;
            while (j < n && ph_prova_css_e_ident(codice[j]))
                j++;
            if (valore) {
                if (j < n && codice[j] == '(') {
                    if (ph_prova_css_funzione_colore(codice + i, j - i))
                        che = "funzione colore";
                } else if (ph_prova_css_nome_colore(codice + i, j - i)) {
                    che = "colore con nome";
                }
            }
            avanza = j - i;
        }
        if (che != NULL) {
            Trovato f = {r, che};
            g_array_append_val(trovati, f);
                    }
        i += avanza;
    }
    /* L'eccezione scusa UNA dichiarazione (anche con piu' colori, come un gradiente): se sulla riga ne
     * iniziano due o piu' (anche con un solo ';', se l'ultima non lo ha) non si sa a quale vale. */
    for (guint k = 0; k < trovati->len; k++) {
        const Trovato *f = &g_array_index(trovati, Trovato, k);
        bool scusato = marca[f->riga] == 1 && semi[f->riga] <= 1 && dich_riga[f->riga] <= 1;
        if (scusato)
            continue;
        viol++;
        if (stampa) {
            const char *e = strchr(inizio[f->riga], '\n');
            fprintf(stderr, "%s:%d: %s: %.*s\n", nome, f->riga, f->che,
                    (int)(e != NULL ? (size_t)(e - inizio[f->riga]) : strlen(inizio[f->riga])),
                    inizio[f->riga]);
        }
    }
    g_array_unref(trovati);
    g_free(dich_riga);
    g_free(codice);
    g_free(marca);
    g_free(semi);
    g_free(inizio);
    return viol;
}

/* Come sopra, sul foglio di stile di una risorsa GResource (il pezzo deve aver gia'
 * registrato le sue risorse). Una risorsa mancante conta come una violazione. */
static inline int ph_prova_css_risorsa(const char *percorso, const char *nome, bool stampa)
{
    GBytes *b = g_resources_lookup_data(percorso, G_RESOURCE_LOOKUP_FLAGS_NONE, NULL);
    if (b == NULL) {
        fprintf(stderr, "%s: risorsa %s non trovata\n", nome, percorso);
        return 1;
    }
    gsize n = 0;
    const char *d = g_bytes_get_data(b, &n);
    char *copia = g_strndup(d, n); /* le risorse non sono per forza terminate da NUL */
    int v = ph_prova_css_colori_a_mano(copia, nome, stampa);
    g_free(copia);
    g_bytes_unref(b);
    return v;
}

/* Prova il rilevatore su esempi finti. Torna il numero di casi sbagliati (0 = tutto bene). */
static inline int ph_prova_css_autoprova(void)
{
    static const struct {
        const char *css;
        int attese;
    } caso[] = {
        /* Ognuno di questi deve scattare (una violazione). */
        {"a { color: #fff; }", 1},
        {"a { color: #FFFFFF; }", 1},
        {"a { color: #ffffff80; }", 1},
        {"a { color: #abcd; }", 1},
        {"a { color: rgb(1,2,3); }", 1},
        {"a { color: rgba(1,2,3,.5); }", 1},
        {"a { color: hsl(1,2%,3%); }", 1},
        {"a { color: hsla(1,2%,3%,.5); }", 1},
        {"a { color: hwb(1 2% 3%); }", 1},
        {"a { color: lab(1 2 3); }", 1},
        {"a { color: oklab(1 2 3); }", 1},
        {"a { color: lch(1 2 3); }", 1},
        {"a { color: oklch(1 2 3); }", 1},
        {"a { color: color(srgb 1 1 1); }", 1},
        {"a { color: color-mix(in srgb, white, black); }", 3},
        {"@define-color brand #123456;\n", 1},
        {"a { color: @brand; }", 1},
        {"a { color: alpha(@x, .5); }", 2},
        {"a { color: shade(@x, .5); }", 2},
        {"a { color: mix(@x, @y, .5); }", 3},
        {"a { color: lighter(@x); }", 2},
        {"a { color: darker(@x); }", 2},
        {"a { color: white; }", 1},
        {"a { color: Black; }", 1},
        {"a { color: gray; }", 1},
        {"a { color: grey; }", 1},
        {"a { color: red; }", 1},
        {"a { color: green; }", 1},
        {"a { color: blue; }", 1},
        {"a { color: yellow; }", 1},
        {"a { color: orange; }", 1},
        {"a { color: silver; }", 1},
        {"a { box-shadow: 0 1px 2px rgba(0,0,0,.2); }", 1},
        {"a { background: linear-gradient(red, blue); }", 2},
        {"/* x */ a { color: #fff; }", 1},
        {"a {\n  background:\n    #fff;\n}\n", 1},
        /* Eccezione: sulla stessa riga, con il motivo, una dichiarazione. */
        {"a { color: #fff; } /* colore-dato: tinta dell'icona */", 0},
        {"a {\n  color: #fff; /* colore-dato: tinta */\n}\n", 0},
        {"a {\n  color: rgba(0,0,0,.2); /* colore-dato: ombra */\n  margin: 0;\n}\n", 0},
        {"a { color: whitesmoke; }", 1},
        {"a { color: DarkGray; }", 1},
        {"a { color: crimson; }", 1},
        {"a { color: rebeccapurple; }", 1},
        /* Eccezione che non vale. */
        {"a { color: #fff; background: #000 } /* colore-dato: due dichiarazioni, un solo punto e virgola */", 2},
        {"a { background: linear-gradient(#111, #222); } /* colore-dato: un gradiente, due colori */", 0},
        {"a { color: #fff; } /* colore-dato: */", 2},
        {"a { color: #fff; margin: 0; } /* colore-dato: due dichiarazioni */", 1},
        {"a {\n  /* colore-dato: sulla riga sbagliata */\n  color: #fff;\n}\n", 1},
        {"a {\n  color: #fff; /* colore-dato: una */\n  background: #000;\n}\n", 1},
        /* Non devono scattare. */
        {"a { color: var(--ph-testo); }", 0},
        {"a { color: transparent; border-color: currentColor; background: none; margin: inherit; }", 0},
        {"/* #fff rgb(1,2,3) white */ a { color: var(--ph-x); }", 0},
        {"/* a\n #fff dentro un commento lungo\n rgba(1,2,3,.5)\n*/\na { margin: 0; }\n", 0},
        {"a { color: var(--ph-x) /* #fff white rgba(1,2,3,.5) */; }", 0},
        {"a { color: var(--ph-x) /* a\n #fff\n rgb(1,2,3) white */; }", 0},
        {"#add { margin: 0; }", 0},
        {"#add:hover { color: var(--ph-x); }", 0},
        {"#barra .rosso { margin: 0; }", 0},
        {"a { color: var(--ph-white); }", 0},
        {"a { content: \"#fff\"; }", 0},
        {"a { font-size: 10px; margin: 0 4px; border-radius: 12px; }", 0},
        {"a { color: #abcxyz; }", 0},
        {"@media (min-width: 10px) { a { color: var(--ph-x); } }", 0},
        {"", 0},
    };
    int sbagliati = 0;
    for (size_t k = 0; k < G_N_ELEMENTS(caso); k++) {
        int v = ph_prova_css_colori_a_mano(caso[k].css, "autoprova", false);
        if (v != caso[k].attese) {
            fprintf(stderr, "prova_css: autoprova caso %zu: attese %d violazioni, trovate %d: %s\n", k,
                    caso[k].attese, v, caso[k].css);
            sbagliati++;
        }
    }
    return sbagliati;
}

#endif
