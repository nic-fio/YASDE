# Sito di YASDE (prova)

`index.html`, `docs.html` e `licence.html` in inglese, nella stessa forma del sito gemello scelto dal committente
(tema chiaro e blu notte, fasce alternate, schede bianche). Parti comuni in `parti/` e `stile.css`, inseriti nelle
pagine. I testi vengono solo dai manuali e dal README. Nessuna risorsa esterna, nessun cookie; stile e immagini
stanno dentro la pagina (data URI).

- Sorgenti: i tre modelli con segnaposto, `parti/`, `stile.css`, `logo.png`, `costruisci.py`.
- Rigenerare: `python3 sito/costruisci.py` (rifà `sito/pubblico/`, non in git). Da rilanciare dopo ogni rigenerazione
  dei manuali, che copia da `docs/`. Per generarlo non serve nessun file fuori dal progetto; serve Pillow (PIL) per il logo.
- Guardare in locale: `cd sito/pubblico && python3 -m http.server 8000`, poi `http://localhost:8000`.
- Il sito NON ospita i file del programma: i pulsanti di scaricamento puntano agli allegati della release su GitHub
  (`.../releases/download/v<versione>/...`, indirizzo del repository da `git remote get-url origin`). Finché la release è
  una bozza i collegamenti danno «non trovato»: è previsto.
- Logo: solo `sito/logo.png` (limite 5 MB), che Pillow ridisegna da capo: ne ricava le misure (doppia densità) per
  barra, piede, testata e icona della pagina. Un `sito/logo.svg` NON è ammesso e fa uscire lo script con errore: se il
  logo è un SVG va prima convertito in PNG. L'originale completo è in `loghi/` nella radice del progetto.
- Indirizzo e contatto, decisi dal committente, sono i predefiniti in testa a `costruisci.py`
  (`INDIRIZZO_PREDEFINITO`, `CONTATTO_PREDEFINITO`): indirizzo canonico, `og:url` e JSON-LD, e il contatto nel piè di
  pagina, in `docs.html` e nel riquadro dello stato. Si possono cambiare con `YASDE_SITO_INDIRIZZO` e
  `YASDE_SITO_CONTATTO`; per generare una copia senza l'uno o senza l'altro si imposta la variabile a vuoto, ad
  esempio `YASDE_SITO_CONTATTO= python3 sito/costruisci.py`. Il formato è convalidato (indirizzo `https://<dominio>/`,
  email semplice).
- `YASDE_SITO_USCITA` cambia la cartella di uscita (per le prove): lo script svuota la cartella indicata, quindi la
  accetta solo se non esiste, è vuota o contiene il file vuoto `.uscita-sito-yasde` (contrassegno scritto dallo script in
  ogni cartella che genera, `sito/pubblico` compresa; non va pubblicato). Rifiuta sempre `/`, la home, la radice del
  progetto, cartelle dentro il progetto (salvo `sito/pubblico`), file e collegamenti simbolici, anche per il percorso
  predefinito.

Non ancora deciso: il modo di pubblicazione.

## Pubblicare

`YASDE_SITO_SERVER=<utente>@<server> sito/pubblica.sh` rigenera il sito e lo copia sul server web, al posto delle pagine
che ci sono. Il server non si scrive nei file. Lo script non prepara il server: la prima volta vanno creati a mano la
cartella `/srv/www/yasde.nicfio.it` (di proprietà dell'utente che pubblica) e, nella configurazione del server web, il
blocco del sito uguale a quello degli altri (radice nella cartella, file statici); poi si ricarica il server web, che
ottiene da solo il certificato. Servono anche il record DNS `yasde.nicfio.it` verso il server e l'alias email del contatto.

I pulsanti di scaricamento portano alla release su GitHub: perché funzionino la release della versione deve essere
pubblica (non in bozza).

Serve una shell POSIX come shell di accesso dell'utente sul server.
