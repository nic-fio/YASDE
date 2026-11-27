# Pezzo shell-finestra-app (F-05): le finestre interne delle app. Vedi COMPITO.md.
# Definisce shell_finestra_app_dep. Si prova con: strumenti/prova.sh shell-finestra-app
# La superficie: quella vera se c'e' in questa compilazione, altrimenti la
# finta del backend finto (stesso contratto, src/shell/superficie.h).
if is_variable('shell_superficie_dep')
  superficie_per_finestre = shell_superficie_dep
else
  superficie_per_finestre = declare_dependency(link_with: shell_superficie_finta_lib)
endif
risorse_finestra_app = gnome.compile_resources('risorse-finestra-app', 'finestra_app.gresource.xml',
  c_name: 'ph_risorse_finestra_app', extra_args: ['--manual-register'])
risorse_finestra_app_lib = static_library('risorse_finestra_app', risorse_finestra_app,
  c_args: shell_args_risorse, dependencies: gtk4_dep)

shell_finestra_app_lib = static_library('shell_finestra_app', ['finestra_app.c', 'logica.c'],
  objects: risorse_finestra_app_lib.extract_all_objects(recursive: false),
  include_directories: inc, dependencies: [shell_comune_dep, finestre_dep, superficie_per_finestre, libm_dep])
shell_finestra_app_dep = declare_dependency(link_with: shell_finestra_app_lib, include_directories: inc,
  dependencies: [shell_comune_dep, finestre_dep, superficie_per_finestre, libm_dep])

# Variante per il demo: stessi sorgenti piu' gli aiuti che riproducono gli stati
# senza mouse (PH_FINESTRA_APP_DEMO); il pezzo vero non li contiene.
shell_finestra_app_demo_lib = static_library('shell_finestra_app_demo', ['finestra_app.c', 'logica.c'],
  objects: risorse_finestra_app_lib.extract_all_objects(recursive: false),
  c_args: ['-DPH_FINESTRA_APP_DEMO'],
  include_directories: inc, dependencies: [shell_comune_dep, finestre_dep, superficie_per_finestre, libm_dep])
shell_finestra_app_demo_dep = declare_dependency(link_with: shell_finestra_app_demo_lib, include_directories: inc,
  dependencies: [shell_comune_dep, finestre_dep, superficie_per_finestre, libm_dep])

# Prova di logica, senza schermo.
test('shell-finestra-app-logica', executable('prova_finestra_app', 'prova_finestra_app.c', dependencies: shell_finestra_app_dep),
  env: shell_ambiente_logica, suite: 'shell', timeout: 60)

# Demo sopra il backend finto (strumenti/demo-sul-tablet.sh finestra-app); con --prova
# e' anche la prova con schermo (saltata se nel container non c'e' broadway).
demo_finestra_app = executable('demo-finestra-app', 'demo.c', dependencies: [shell_finestra_app_demo_dep, shell_finto_dep])
test('shell-finestra-app-schermo', demo_finestra_app, args: ['--prova', '9000', '--compat'],
  env: shell_ambiente_schermo, suite: 'shell', timeout: 60)

# Destro e centrale del mouse sul contenuto delle finestre (registro dei tocchi mandati alla superficie).
test('shell-finestra-app-mouse', demo_finestra_app, args: ['--prova', '60000', '--mouse'],
  env: shell_ambiente_schermo, suite: 'shell', timeout: 120)

# Pagine (PIANO.md 8.7): apertura con la pagina, finestra aperta o minimizzata, errore, fuori elenco.
test('shell-finestra-app-pagine', demo_finestra_app, args: ['--prova', '15000', '--pagine'],
  env: shell_ambiente_schermo, suite: 'shell', timeout: 60)

# Prove del vetro (sfocatura dietro il titolo): modalita', alberi dei nodi, paintable, chiusure, riposo.
test('shell-finestra-app-vetro', demo_finestra_app, args: ['--prova', '200000', '--vetro'],
  env: shell_ambiente_schermo, suite: 'shell', timeout: 300)

# Menu «altri comandi» come vetro chiaro, con la sfocatura della shell accesa dall'avvio.
test('shell-finestra-app-vetro-menu', demo_finestra_app, args: ['--prova', '60000', '--vetro-menu'],
  env: shell_ambiente_schermo, suite: 'shell', timeout: 120)
