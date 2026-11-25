/* Strumento a mano (non e' un test): cerca i telefoni in rete per 5 secondi e
 * stampa cio' che trova. Su stderr scrive la traccia: domande mandate su ogni
 * interfaccia e pacchetti ricevuti, con il tempo in millisecondi. */
#include <stdio.h>
#include <stdlib.h>

#include "scoperta/scoperta.h"
#include "scoperta/scoperta_interno.h"

int main(int argc, char **argv)
{
    int durata = 5000;
    if (argc > 1)
        durata = atoi(argv[1]) * 1000;
    if (durata <= 0)
        durata = 5000;
    PhTelefonoTrovato trovati[32];
    PhErrore e;
    int n = ph_scoperta_cerca_traccia(durata, trovati, 32, &e, stderr);
    if (n < 0) {
        fprintf(stderr, "errore: %s\n", e.testo);
        return 1;
    }
    for (int i = 0; i < n; i++)
        printf("%s %s:%u %s\n", trovati[i].nome, trovati[i].indirizzo, trovati[i].porta,
               trovati[i].abbinamento ? "abbinamento" : "collegamento");
    printf("trovati: %d\n", n);
    return 0;
}
