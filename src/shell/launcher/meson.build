# Pezzo shell-launcher (F-04): il launcher a griglia. Vedi COMPITO.md.
# Definisce shell_launcher_dep. Si prova con: strumenti/prova.sh shell-launcher
risorse_launcher = gnome.compile_resources('risorse-launcher', 'launcher.gresource.xml',
  c_name: 'ph_risorse_launcher', extra_args: ['--manual-register'])
risorse_launcher_lib = static_library('risorse_launcher', risorse_launcher,
  c_args: shell_args_risorse, dependencies: gtk4_dep)

shell_launcher_lib = static_library('shell_launcher', ['launcher.c', 'logica.c'],
  objects: risorse_launcher_lib.extract_all_objects(recursive: false),
  include_directories: inc, dependencies: [shell_comune_dep])
shell_launcher_dep = declare_dependency(link_with: shell_launcher_lib, include_directories: inc,
  dependencies: [shell_comune_dep])

# Prova di logica, senza schermo.
test('shell-launcher-logica', executable('prova_launcher', ['prova_launcher.c', 'logica.c'], dependencies: shell_launcher_dep),
  env: shell_ambiente_logica, suite: 'shell', timeout: 60)

# Demo sopra il backend finto (strumenti/demo-sul-tablet.sh launcher); con --prova
# e' anche la prova con schermo (saltata se nel container non c'e' broadway).
demo_launcher = executable('demo-launcher', ['demo.c', 'prova_demo.c'], dependencies: [shell_launcher_dep, shell_finto_dep])
test('shell-launcher-schermo', demo_launcher, args: ['--prova', '8000', '--misura', '1000x500'],
  env: shell_ambiente_schermo, suite: 'shell', timeout: 60)
