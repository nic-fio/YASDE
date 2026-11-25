# Pezzo shell-superficie (4.2, 4.3, 4.5, 5.5): il widget che mostra un flusso
# video (src/shell/superficie.h). Vedi COMPITO.md. Definisce shell_superficie_dep.
# Si prova con: strumenti/prova.sh shell-superficie
risorse_superficie = gnome.compile_resources('risorse-superficie', 'superficie.gresource.xml',
  c_name: 'ph_risorse_superficie', extra_args: ['--manual-register'])
risorse_superficie_lib = static_library('risorse_superficie', risorse_superficie,
  c_args: shell_args_risorse, dependencies: gtk4_dep)

shell_superficie_lib = static_library('shell_superficie', ['superficie.c', 'logica.c'],
  objects: risorse_superficie_lib.extract_all_objects(recursive: false),
  include_directories: inc, dependencies: [shell_comune_dep])
shell_superficie_dep = declare_dependency(link_with: shell_superficie_lib, include_directories: inc,
  dependencies: [shell_comune_dep])

# Prova di logica, senza schermo.
test('shell-superficie-logica', executable('prova_superficie', 'prova_superficie.c',
  dependencies: shell_superficie_dep), env: shell_ambiente_logica, suite: 'shell', timeout: 60)

# Demo sopra il backend finto (strumenti/demo-sul-tablet.sh superficie); con
# --prova e' anche la prova con schermo (saltata se manca broadway).
demo_superficie = executable('demo-superficie', 'demo.c',
  dependencies: [shell_superficie_dep, shell_finto_dep])
test('shell-superficie-schermo', demo_superficie, args: ['--prova', '2000'],
  env: shell_ambiente_schermo, suite: 'shell', timeout: 60)
