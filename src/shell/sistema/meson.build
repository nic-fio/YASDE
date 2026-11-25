# Pezzo shell-sistema (F-06, F-07, F-08): notifiche e avvisi a comparsa, stato del telefono, volume. Vedi COMPITO.md.
# Definisce shell_sistema_dep. Si prova con: strumenti/prova.sh shell-sistema
risorse_sistema = gnome.compile_resources('risorse-sistema', 'sistema.gresource.xml',
  c_name: 'ph_risorse_sistema', extra_args: ['--manual-register'])
risorse_sistema_lib = static_library('risorse_sistema', risorse_sistema,
  c_args: shell_args_risorse, dependencies: gtk4_dep)

shell_sistema_lib = static_library('shell_sistema',
  ['sistema.c', 'logica.c', 'notifiche.c', 'toast.c', 'telefono.c', 'volume.c', 'pulsanti.c'],
  objects: risorse_sistema_lib.extract_all_objects(recursive: false),
  include_directories: inc, dependencies: [shell_comune_dep])
shell_sistema_dep = declare_dependency(link_with: shell_sistema_lib, include_directories: inc,
  dependencies: [shell_comune_dep])

# Prova di logica, senza schermo.
test('shell-sistema-logica', executable('prova_sistema', 'prova_sistema.c', dependencies: shell_sistema_dep),
  env: shell_ambiente_logica, suite: 'shell', timeout: 60)

# Demo sopra il backend finto (strumenti/demo-sul-tablet.sh sistema); con --prova
# e' anche la prova con schermo (saltata se nel container non c'e' broadway).
demo_sistema = executable('demo-sistema', 'demo.c', dependencies: [shell_sistema_dep, shell_finto_dep])
test('shell-sistema-schermo', demo_sistema, args: ['--prova', '2000'],
  env: shell_ambiente_schermo, suite: 'shell', timeout: 60)
