#include "comune/buffer.h"
#include "comune/errore.h"

#include <assert.h>
#include <string.h>

int main(void)
{
    PhBuffer b = PH_BUFFER_VUOTO;
    assert(ph_buffer_aggiungi(&b, "ciao", 4) == 0);
    assert(ph_buffer_aggiungi(&b, " mondo", 6) == 0);
    assert(b.lunghezza == 10 && strcmp((char *)b.dati, "ciao mondo") == 0);
    for (int i = 0; i < 1000; i++)
        assert(ph_buffer_aggiungi(&b, "x", 1) == 0);
    assert(b.lunghezza == 1010 && b.dati[1010] == '\0');
    ph_buffer_svuota(&b);
    assert(b.lunghezza == 0 && b.dati[0] == '\0');
    ph_buffer_libera(&b);
    assert(b.dati == NULL);

    PhErrore e;
    assert(ph_errore(&e, "porta %d chiusa", 5555) == -1);
    assert(strcmp(e.testo, "porta 5555 chiusa") == 0);
    assert(ph_errore_contesto(&e, "collegamento a %s", "telefono") == -1);
    assert(strcmp(e.testo, "collegamento a telefono: porta 5555 chiusa") == 0);
    assert(ph_errore(NULL, "niente") == -1);
    return 0;
}
