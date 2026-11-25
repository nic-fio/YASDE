# Backend finto dei servizi (finto.h): serve ai demo e alle prove di tutti i
# pezzi della shell, quindi entra sempre (anche con pezzo=tutto).
shell_finto_lib = static_library('shell_finto', ['finto.c', 'flusso_finto.c', 'comandi.c'],
  include_directories: inc, dependencies: shell_comune_dep)
shell_finto_dep = declare_dependency(link_with: shell_finto_lib, include_directories: inc,
  dependencies: shell_comune_dep)

# Superficie finta (superficie.h): la collega finestra-app solo se il pezzo
# superficie non c'e' in questa compilazione (altrimenti simboli doppi).
shell_superficie_finta_lib = static_library('shell_superficie_finta', 'superficie_finta.c',
  include_directories: inc, dependencies: shell_comune_dep)

test('shell-finto', executable('prova_finto', 'prova_finto.c', dependencies: shell_finto_dep),
  env: shell_ambiente_logica, suite: 'shell', timeout: 120)

demo_finto = executable('demo-finto', 'demo_finto.c',
  link_with: shell_superficie_finta_lib, dependencies: shell_finto_dep)
test('shell-finto-schermo', demo_finto, args: ['--prova', '1500'],
  env: shell_ambiente_schermo, suite: 'shell', timeout: 60)
