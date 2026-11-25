# Pezzo shell-barra (F-03): la barra in basso. Vedi COMPITO.md.
# Definisce shell_barra_dep. Si prova con: strumenti/prova.sh shell-barra
risorse_barra = gnome.compile_resources('risorse-barra', 'barra.gresource.xml',
  c_name: 'ph_risorse_barra', extra_args: ['--manual-register'])
risorse_barra_lib = static_library('risorse_barra', risorse_barra,
  c_args: shell_args_risorse, dependencies: gtk4_dep)

shell_barra_lib = static_library('shell_barra', ['barra.c', 'logica.c'],
  objects: risorse_barra_lib.extract_all_objects(recursive: false),
  include_directories: inc, dependencies: [shell_comune_dep])
shell_barra_dep = declare_dependency(link_with: shell_barra_lib, include_directories: inc,
  dependencies: [shell_comune_dep])

# Prova di logica, senza schermo.
test('shell-barra-logica', executable('prova_barra', 'prova_barra.c', dependencies: shell_barra_dep),
  env: shell_ambiente_logica, suite: 'shell', timeout: 60)

# Demo sopra il backend finto (strumenti/demo-sul-tablet.sh barra); con --prova
# e' anche la prova con schermo (saltata se nel container non c'e' broadway).
demo_barra = executable('demo-barra', 'demo.c', dependencies: [shell_barra_dep, shell_finto_dep])
test('shell-barra-schermo', demo_barra, args: ['--prova', '2000'],
  env: shell_ambiente_schermo, suite: 'shell', timeout: 60)
