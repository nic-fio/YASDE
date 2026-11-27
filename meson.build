project('yasde', 'c', version: '2.3.0',
  default_options: ['c_std=c17', 'warning_level=3', 'werror=true', 'buildtype=debugoptimized'])

# Ogni pezzo vive nella sua cartella con il suo meson.build e definisce:
#   <pezzo>_dep  (declare_dependency con la sua libreria statica)
# e i suoi test (test('nome', eseguibile)).
# Con -Dpezzo=<nome> si compila solo quel pezzo (piu' comune); i pezzi da cui
# dipende li sostituisce nei test con un finto scritto nella sua cartella.
add_project_arguments('-D_GNU_SOURCE', language: 'c')
inc = include_directories('src')
pezzo = get_option('pezzo')
fs = import('fs')

subdir('src/comune')

pezzi = ['adb', 'file', 'scoperta', 'abbinamento', 'componente', 'video',
         'audio', 'dati', 'finestre', 'config', 'trasferimenti', 'collegamento', 'meteo']
# Con pezzo=tutto entrano solo i pezzi finiti e approvati: un pezzo ancora in
# scrittura non deve rompere la compilazione d'insieme degli altri. Chi
# coordina aggiunge qui un pezzo quando il revisore lo approva.
pezzi_pronti = ['adb', 'file', 'scoperta', 'abbinamento', 'componente', 'video', 'finestre', 'config', 'trasferimenti', 'audio', 'dati', 'collegamento', 'meteo']
# Shell (src/shell/, GTK4): pezzi 'shell-<pezzo>' (CONTRATTI.md). Come per gli
# altri pezzi, con pezzo=tutto entrano solo quelli approvati in shell_pronti;
# il pezzo comune e il backend finto (shell-finto) entrano sempre. Ogni pezzo
# della shell si compila con 'config' e 'finestre' (pura logica gia'
# approvata); l'ossatura anche con tutti i pezzi sotto (servizi veri).
shell_pronti = ['barra', 'trasferimento', 'superficie', 'finestra-app', 'sistema', 'desktop', 'launcher', 'connessione', 'ossatura']
shell_sotto = ['config', 'finestre']
foreach p : pezzi
  dentro = (pezzo == 'tutto' and pezzi_pronti.contains(p)) or pezzo == p
  if pezzo.startswith('shell-')
    dentro = dentro or shell_sotto.contains(p) or pezzo == 'shell-ossatura'
  endif
  if dentro and fs.exists('src' / p / 'meson.build')
    subdir('src' / p)
  endif
endforeach

if (pezzo == 'tutto' or pezzo.startswith('shell-')) and fs.exists('src/shell/meson.build')
  subdir('src/shell')
endif

if pezzo == 'tutto' and fs.exists('strumenti/meson.build')
  subdir('strumenti')
endif
