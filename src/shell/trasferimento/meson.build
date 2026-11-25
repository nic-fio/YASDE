# Pezzo shell-trasferimento (F-09): il trasferimento di file e il selettore di cartelle. Vedi COMPITO.md.
# Definisce shell_trasferimento_dep. Si prova con: strumenti/prova.sh shell-trasferimento
risorse_trasferimento = gnome.compile_resources('risorse-trasferimento', 'trasferimento.gresource.xml',
  c_name: 'ph_risorse_trasferimento', extra_args: ['--manual-register'])
risorse_trasferimento_lib = static_library('risorse_trasferimento', risorse_trasferimento,
  c_args: shell_args_risorse, dependencies: gtk4_dep)

shell_trasferimento_lib = static_library('shell_trasferimento', ['trasferimento.c', 'percorsi.c', 'elenco.c', 'navigatore.c', 'apk.c'],
  objects: risorse_trasferimento_lib.extract_all_objects(recursive: false),
  include_directories: inc, dependencies: [shell_comune_dep])
shell_trasferimento_dep = declare_dependency(link_with: shell_trasferimento_lib, include_directories: inc,
  dependencies: [shell_comune_dep])

# Prova di logica, senza schermo.
test('shell-trasferimento-logica', executable('prova_trasferimento', 'prova_trasferimento.c', dependencies: shell_trasferimento_dep),
  env: shell_ambiente_logica, suite: 'shell', timeout: 60)

# Demo sopra il backend finto (strumenti/demo-sul-tablet.sh trasferimento); con --prova
# e' anche la prova con schermo (saltata se nel container non c'e' broadway).
demo_trasferimento = executable('demo-trasferimento', ['demo.c', 'prova.c'], dependencies: [shell_trasferimento_dep, shell_finto_dep])
test('shell-trasferimento-schermo', demo_trasferimento, args: ['--prova', '9000'],
  env: shell_ambiente_schermo + ['PH_DEMO_SEQ=veloce'], suite: 'shell', timeout: 60)
