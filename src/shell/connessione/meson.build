# Pezzo shell-connessione (prima connessione: scoperta, abbinamento con il
# codice, attese ed errori). Vedi COMPITO.md. Definisce shell_connessione_dep.
# Si prova con: strumenti/prova.sh shell-connessione
risorse_connessione = gnome.compile_resources('risorse-connessione', 'connessione.gresource.xml',
  c_name: 'ph_risorse_connessione', extra_args: ['--manual-register'])
risorse_connessione_lib = static_library('risorse_connessione', risorse_connessione,
  c_args: shell_args_risorse, dependencies: gtk4_dep)

shell_connessione_lib = static_library('shell_connessione', ['connessione.c', 'logica_connessione.c'],
  objects: risorse_connessione_lib.extract_all_objects(recursive: false),
  include_directories: inc, dependencies: [shell_comune_dep])
shell_connessione_dep = declare_dependency(link_with: shell_connessione_lib, include_directories: inc,
  dependencies: [shell_comune_dep])

# Prova di logica, senza schermo.
test('shell-connessione-logica', executable('prova_connessione', 'prova_connessione.c',
  dependencies: shell_connessione_dep), env: shell_ambiente_logica, suite: 'shell', timeout: 60)

# Demo sopra il backend finto; con --prova e' anche la prova con schermo
# (saltata se manca broadway).
demo_connessione = executable('demo-connessione', 'demo.c',
  dependencies: [shell_connessione_dep, shell_finto_dep])
test('shell-connessione-schermo', demo_connessione, args: ['--prova', '2000'],
  env: shell_ambiente_schermo, suite: 'shell', timeout: 60)

# Prove con i widget veri (saltate senza schermo, come la precedente): digitazione
# e incolla, doppio Invio, campo vuoto dopo ogni esito, ritorno a NON_ABBINATO; e
# la chiusura della finestra durante «Abbinamento in corso…» (sotto AddressSanitizer).
test('shell-connessione-widget', demo_connessione, args: ['--verifica', '--prova', '30000'],
  env: shell_ambiente_schermo, suite: 'shell', timeout: 60)
test('shell-connessione-chiusura', demo_connessione, args: ['--mostra', 'chiudi-in-corso', '--prova', '30000'],
  env: shell_ambiente_schermo, suite: 'shell', timeout: 60)
